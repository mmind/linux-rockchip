// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2022 Rockchip Electronics Co., Ltd.
 * Copyright (c) 2024 Collabora Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 * Author: Cristian Ciocaltea <cristian.ciocaltea@collabora.com>
 */
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#include <drm/bridge/dw_hdmi_qp.h>
#include <drm/display/drm_hdmi_state_helper.h>
#include <drm/display/drm_scdc_helper.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_edid.h>

#include <sound/hdmi-codec.h>

#include "dw-hdmi-qp.h"

#define DDC_CI_ADDR		0x37
#define DDC_SEGMENT_ADDR	0x30

#define SCDC_MIN_SOURCE_VERSION	0x1

#define HDMI14_MAX_TMDSCLK	340000000

struct dw_hdmi_qp_i2c {
	struct i2c_adapter	adap;

	struct mutex		lock;	/* used to serialize data transfers */
	struct completion	cmp;
	u8			stat;

	u8			slave_reg;
	bool			is_regaddr;
	bool			is_segment;
};

struct dw_hdmi_qp {
	struct drm_bridge bridge;

	struct device *dev;
	struct dw_hdmi_qp_i2c *i2c;

	struct {
		const struct dw_hdmi_qp_phy_ops *ops;
		void *data;
	} phy;

	struct clk *ref_clk;

	struct drm_connector *curr_conn;
	unsigned long long pix_clock;

	struct regmap *regm;
};

/* Filter out invalid setups to avoid configuring SCDC and scrambling */
static bool dw_hdmi_qp_support_scdc(struct dw_hdmi_qp *hdmi,
				    const struct drm_display_info *display)
{
	/* Disable if no DDC bus */
	if (!hdmi->bridge.ddc)
		return false;

	/* Disable if SCDC is not supported, or if an HF-VSDB block is absent */
	if (!display->hdmi.scdc.supported ||
	    !display->hdmi.scdc.scrambling.supported)
		return false;

	/*
	 * Disable if display only support low TMDS rates and scrambling
	 * for low rates is not supported either
	 */
	if (!display->hdmi.scdc.scrambling.low_rates &&
	    display->max_tmds_clock <= 340000)
		return false;

	return true;
}

int dw_hdmi_qp_set_refclk_rate(struct dw_hdmi_qp *hdmi, unsigned long rate)
{
	return clk_set_rate(hdmi->ref_clk, rate);
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_set_refclk_rate);

/*
 * HDMI2.0 Specifies the following procedure for High TMDS Bit Rates:
 * - The Source shall suspend transmission of the TMDS clock and data
 *
 * - The Source shall write to the TMDS_Bit_Clock_Ratio bit to change it
 *   from a 0 to a 1 or from a 1 to a 0
 *
 * - The Source shall allow a minimum of 1 ms and a maximum of 100 ms from
 *   the time the TMDS_Bit_Clock_Ratio bit is written until resuming
 *   transmission of TMDS clock and data
 *
 * To respect the 100ms max delay, the dw_hdmi_qp_set_high_tmds_clock_ratio()
 * helper should be called right before enabling the TMDS Clock and Data in
 * the PHY configuration callback.
 */
void dw_hdmi_qp_set_high_tmds_clock_ratio(struct dw_hdmi_qp *hdmi,
					  const struct drm_display_info *display)
{
	bool set;

	if (hdmi->curr_conn && dw_hdmi_qp_support_scdc(hdmi, display)) {
		set = (hdmi->pix_clock > HDMI14_MAX_TMDSCLK);
		drm_scdc_set_high_tmds_clock_ratio(hdmi->curr_conn, set);
	}
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_set_high_tmds_clock_ratio);

static void dw_hdmi_qp_write(struct dw_hdmi_qp *hdmi, unsigned int val,
			     int offset)
{
	regmap_write(hdmi->regm, offset, val);
}

static unsigned int dw_hdmi_qp_read(struct dw_hdmi_qp *hdmi, int offset)
{
	unsigned int val = 0;

	regmap_read(hdmi->regm, offset, &val);

	return val;
}

static void dw_hdmi_qp_mod(struct dw_hdmi_qp *hdmi, unsigned int data,
			   unsigned int mask, unsigned int reg)
{
	regmap_update_bits(hdmi->regm, reg, mask, data);
}

static int dw_hdmi_qp_i2c_read(struct dw_hdmi_qp *hdmi,
			       unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	int stat;

	if (!i2c->is_regaddr) {
		dev_dbg(hdmi->dev, "set read register address to 0\n");
		i2c->slave_reg = 0x00;
		i2c->is_regaddr = true;
	}

	while (length--) {
		reinit_completion(&i2c->cmp);

		dw_hdmi_qp_mod(hdmi, i2c->slave_reg++ << 12, I2CM_ADDR,
			       I2CM_INTERFACE_CONTROL0);

		if (i2c->is_segment)
			dw_hdmi_qp_mod(hdmi, I2CM_EXT_READ, I2CM_WR_MASK,
				       I2CM_INTERFACE_CONTROL0);
		else
			dw_hdmi_qp_mod(hdmi, I2CM_FM_READ, I2CM_WR_MASK,
				       I2CM_INTERFACE_CONTROL0);

		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat) {
			dev_err(hdmi->dev, "i2c read timed out\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EAGAIN;
		}

		/* Check for error condition on the bus */
		if (i2c->stat & I2CM_NACK_RCVD_IRQ) {
			dev_err(hdmi->dev, "i2c read error\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EIO;
		}

		*buf++ = dw_hdmi_qp_read(hdmi, I2CM_INTERFACE_RDDATA_0_3) & 0xff;
		dw_hdmi_qp_mod(hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
	}

	i2c->is_segment = false;

	return 0;
}

static int dw_hdmi_qp_i2c_write(struct dw_hdmi_qp *hdmi,
				unsigned char *buf, unsigned int length)
{
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	int stat;

	if (!i2c->is_regaddr) {
		/* Use the first write byte as register address */
		i2c->slave_reg = buf[0];
		length--;
		buf++;
		i2c->is_regaddr = true;
	}

	while (length--) {
		reinit_completion(&i2c->cmp);

		dw_hdmi_qp_write(hdmi, *buf++, I2CM_INTERFACE_WRDATA_0_3);
		dw_hdmi_qp_mod(hdmi, i2c->slave_reg++ << 12, I2CM_ADDR,
			       I2CM_INTERFACE_CONTROL0);
		dw_hdmi_qp_mod(hdmi, I2CM_FM_WRITE, I2CM_WR_MASK,
			       I2CM_INTERFACE_CONTROL0);

		stat = wait_for_completion_timeout(&i2c->cmp, HZ / 10);
		if (!stat) {
			dev_err(hdmi->dev, "i2c write time out!\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EAGAIN;
		}

		/* Check for error condition on the bus */
		if (i2c->stat & I2CM_NACK_RCVD_IRQ) {
			dev_err(hdmi->dev, "i2c write nack!\n");
			dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);
			return -EIO;
		}

		dw_hdmi_qp_mod(hdmi, 0, I2CM_WR_MASK, I2CM_INTERFACE_CONTROL0);
	}

	return 0;
}

static int dw_hdmi_qp_i2c_xfer(struct i2c_adapter *adap,
			       struct i2c_msg *msgs, int num)
{
	struct dw_hdmi_qp *hdmi = i2c_get_adapdata(adap);
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	u8 addr = msgs[0].addr;
	int i, ret = 0;

	if (addr == DDC_CI_ADDR)
		/*
		 * The internal I2C controller does not support the multi-byte
		 * read and write operations needed for DDC/CI.
		 * FIXME: Blacklist the DDC/CI address until we filter out
		 * unsupported I2C operations.
		 */
		return -EOPNOTSUPP;

	for (i = 0; i < num; i++) {
		if (msgs[i].len == 0) {
			dev_err(hdmi->dev,
				"unsupported transfer %d/%d, no data\n",
				i + 1, num);
			return -EOPNOTSUPP;
		}
	}

	mutex_lock(&i2c->lock);

	/* Unmute DONE and ERROR interrupts */
	dw_hdmi_qp_mod(hdmi, I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
		       I2CM_NACK_RCVD_MASK_N | I2CM_OP_DONE_MASK_N,
		       MAINUNIT_1_INT_MASK_N);

	/* Set slave device address taken from the first I2C message */
	if (addr == DDC_SEGMENT_ADDR && msgs[0].len == 1)
		addr = DDC_ADDR;

	dw_hdmi_qp_mod(hdmi, addr << 5, I2CM_SLVADDR, I2CM_INTERFACE_CONTROL0);

	/* Set slave device register address on transfer */
	i2c->is_regaddr = false;

	/* Set segment pointer for I2C extended read mode operation */
	i2c->is_segment = false;

	for (i = 0; i < num; i++) {
		if (msgs[i].addr == DDC_SEGMENT_ADDR && msgs[i].len == 1) {
			i2c->is_segment = true;
			dw_hdmi_qp_mod(hdmi, DDC_SEGMENT_ADDR, I2CM_SEG_ADDR,
				       I2CM_INTERFACE_CONTROL1);
			dw_hdmi_qp_mod(hdmi, *msgs[i].buf << 7, I2CM_SEG_PTR,
				       I2CM_INTERFACE_CONTROL1);
		} else {
			if (msgs[i].flags & I2C_M_RD)
				ret = dw_hdmi_qp_i2c_read(hdmi, msgs[i].buf,
							  msgs[i].len);
			else
				ret = dw_hdmi_qp_i2c_write(hdmi, msgs[i].buf,
							   msgs[i].len);
		}
		if (ret < 0)
			break;
	}

	if (!ret)
		ret = num;

	/* Mute DONE and ERROR interrupts */
	dw_hdmi_qp_mod(hdmi, 0, I2CM_OP_DONE_MASK_N | I2CM_NACK_RCVD_MASK_N,
		       MAINUNIT_1_INT_MASK_N);

	mutex_unlock(&i2c->lock);

	return ret;
}

static u32 dw_hdmi_qp_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm dw_hdmi_qp_algorithm = {
	.master_xfer	= dw_hdmi_qp_i2c_xfer,
	.functionality	= dw_hdmi_qp_i2c_func,
};

static struct i2c_adapter *dw_hdmi_qp_i2c_adapter(struct dw_hdmi_qp *hdmi)
{
	struct dw_hdmi_qp_i2c *i2c;
	struct i2c_adapter *adap;
	int ret;

	i2c = devm_kzalloc(hdmi->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return ERR_PTR(-ENOMEM);

	mutex_init(&i2c->lock);
	init_completion(&i2c->cmp);

	adap = &i2c->adap;
	adap->owner = THIS_MODULE;
	adap->dev.parent = hdmi->dev;
	adap->algo = &dw_hdmi_qp_algorithm;
	strscpy(adap->name, "DesignWare HDMI QP", sizeof(adap->name));

	i2c_set_adapdata(adap, hdmi);

	ret = devm_i2c_add_adapter(hdmi->dev, adap);
	if (ret) {
		dev_warn(hdmi->dev, "cannot add %s I2C adapter\n", adap->name);
		devm_kfree(hdmi->dev, i2c);
		return ERR_PTR(ret);
	}

	hdmi->i2c = i2c;
	dev_info(hdmi->dev, "registered %s I2C bus driver\n", adap->name);

	return adap;
}

static int dw_hdmi_qp_config_avi_infoframe(struct dw_hdmi_qp *hdmi,
					   const u8 *buffer, size_t len)
{
	u32 val, i, j;

	if (len != HDMI_INFOFRAME_SIZE(AVI)) {
		dev_err(hdmi->dev, "failed to configure avi infoframe\n");
		return -EINVAL;
	}

	/*
	 * DW HDMI QP IP uses a different byte format from standard AVI info
	 * frames, though generally the bits are in the correct bytes.
	 */
	val = buffer[1] << 8 | buffer[2] << 16;
	dw_hdmi_qp_write(hdmi, val, PKT_AVI_CONTENTS0);

	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			if (i * 4 + j >= 14)
				break;
			if (!j)
				val = buffer[i * 4 + j + 3];
			val |= buffer[i * 4 + j + 3] << (8 * j);
		}

		dw_hdmi_qp_write(hdmi, val, PKT_AVI_CONTENTS1 + i * 4);
	}

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_AVI_FIELDRATE, PKTSCHED_PKT_CONFIG1);

	dw_hdmi_qp_mod(hdmi, PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN,
		       PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN, PKTSCHED_PKT_EN);

	return 0;
}

static int dw_hdmi_qp_config_drm_infoframe(struct dw_hdmi_qp *hdmi,
					   const u8 *buffer, size_t len)
{
	u32 val, i;

	if (len != HDMI_INFOFRAME_SIZE(DRM)) {
		dev_err(hdmi->dev, "failed to configure drm infoframe\n");
		return -EINVAL;
	}

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_DRMI_TX_EN, PKTSCHED_PKT_EN);

	val = buffer[1] << 8 | buffer[2] << 16;
	dw_hdmi_qp_write(hdmi, val, PKT_DRMI_CONTENTS0);

	for (i = 0; i <= buffer[2]; i++) {
		if (i % 4 == 0)
			val = buffer[3 + i];
		val |= buffer[3 + i] << ((i % 4) * 8);

		if ((i % 4 == 3) || i == buffer[2])
			dw_hdmi_qp_write(hdmi, val,
					 PKT_DRMI_CONTENTS1 + ((i / 4) * 4));
	}

	dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_DRMI_FIELDRATE, PKTSCHED_PKT_CONFIG1);
	dw_hdmi_qp_mod(hdmi, PKTSCHED_DRMI_TX_EN, PKTSCHED_DRMI_TX_EN,
		       PKTSCHED_PKT_EN);

	return 0;
}

static void dw_hdmi_qp_setup(struct dw_hdmi_qp *hdmi,
			     struct drm_connector *connector)
{
	bool scramb;
	u8 ver;

	if (!connector->display_info.is_hdmi) {
		dev_dbg(hdmi->dev, "%s DVI mode\n", __func__);

		dw_hdmi_qp_mod(hdmi, HDCP2_BYPASS, HDCP2_BYPASS,
			       HDCP2LOGIC_CONFIG0);
		dw_hdmi_qp_mod(hdmi, OPMODE_DVI, OPMODE_DVI, LINK_CONFIG0);

		return;
	}

	dev_dbg(hdmi->dev, "%s HDMI mode\n", __func__);

	dw_hdmi_qp_mod(hdmi, 0, OPMODE_DVI, LINK_CONFIG0);
	dw_hdmi_qp_mod(hdmi, HDCP2_BYPASS, HDCP2_BYPASS, HDCP2LOGIC_CONFIG0);

	scramb = (hdmi->pix_clock > HDMI14_MAX_TMDSCLK);

	if (dw_hdmi_qp_support_scdc(hdmi, &connector->display_info)) {
		if (scramb) {
			drm_scdc_readb(hdmi->bridge.ddc, SCDC_SINK_VERSION, &ver);
			drm_scdc_writeb(hdmi->bridge.ddc, SCDC_SOURCE_VERSION,
					min_t(u8, ver, SCDC_MIN_SOURCE_VERSION));
		}

		drm_scdc_set_high_tmds_clock_ratio(connector, scramb);
		drm_scdc_set_scrambling(connector, scramb);
	}

	dw_hdmi_qp_write(hdmi, scramb, SCRAMB_CONFIG0);
}

static int dw_hdmi_qp_bridge_atomic_check(struct drm_bridge *bridge,
					  struct drm_bridge_state *bridge_state,
					  struct drm_crtc_state *crtc_state,
					  struct drm_connector_state *conn_state)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	int ret;

	ret = drm_atomic_helper_connector_hdmi_check(conn_state->connector,
						     conn_state->state);
	if (ret)
		dev_dbg(hdmi->dev, "%s failed: %d\n", __func__, ret);

	return ret;
}

static void dw_hdmi_qp_bridge_atomic_enable(struct drm_bridge *bridge,
					    struct drm_bridge_state *old_state)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	struct drm_atomic_state *state = old_state->base.state;
	struct drm_connector *connector;
	struct drm_connector_state *conn_state;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	conn_state = drm_atomic_get_new_connector_state(state, connector);

	hdmi->pix_clock = conn_state->hdmi.tmds_char_rate;
	hdmi->curr_conn = connector;

	hdmi->phy.ops->init(hdmi, hdmi->phy.data, &connector->display_info);

	dw_hdmi_qp_setup(hdmi, connector);

	drm_atomic_helper_connector_hdmi_update_infoframes(connector, state);
}

static void dw_hdmi_qp_bridge_atomic_disable(struct drm_bridge *bridge,
					     struct drm_bridge_state *old_state)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	hdmi->curr_conn = NULL;
	hdmi->phy.ops->disable(hdmi, hdmi->phy.data);
}

static enum drm_connector_status
dw_hdmi_qp_bridge_detect(struct drm_bridge *bridge)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	return hdmi->phy.ops->read_hpd(hdmi, hdmi->phy.data);
}

static const struct drm_edid *
dw_hdmi_qp_bridge_edid_read(struct drm_bridge *bridge,
			    struct drm_connector *connector)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;
	const struct drm_edid *drm_edid;

	if (!bridge->ddc)
		return NULL;

	drm_edid = drm_edid_read_ddc(connector, bridge->ddc);
	if (!drm_edid)
		dev_dbg(hdmi->dev, "failed to get edid\n");

	return drm_edid;
}

static int dw_hdmi_qp_bridge_clear_infoframe(struct drm_bridge *bridge,
					     enum hdmi_infoframe_type type)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_AVI_TX_EN | PKTSCHED_GCP_TX_EN,
			       PKTSCHED_PKT_EN);
		break;

	case HDMI_INFOFRAME_TYPE_DRM:
		dw_hdmi_qp_mod(hdmi, 0, PKTSCHED_DRMI_TX_EN, PKTSCHED_PKT_EN);
		break;

	default:
		dev_dbg(hdmi->dev, "Unsupported infoframe type %x\n", type);
	}

	return 0;
}

static int dw_hdmi_qp_bridge_write_infoframe(struct drm_bridge *bridge,
					     enum hdmi_infoframe_type type,
					     const u8 *buffer, size_t len)
{
	struct dw_hdmi_qp *hdmi = bridge->driver_private;

	dw_hdmi_qp_bridge_clear_infoframe(bridge, type);

	switch (type) {
	case HDMI_INFOFRAME_TYPE_AVI:
		return dw_hdmi_qp_config_avi_infoframe(hdmi, buffer, len);

	case HDMI_INFOFRAME_TYPE_DRM:
		return dw_hdmi_qp_config_drm_infoframe(hdmi, buffer, len);

	default:
		dev_dbg(hdmi->dev, "Unsupported infoframe type %x\n", type);
		return 0;
	}
}

static const struct drm_bridge_funcs dw_hdmi_qp_bridge_funcs = {
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_check = dw_hdmi_qp_bridge_atomic_check,
	.atomic_enable = dw_hdmi_qp_bridge_atomic_enable,
	.atomic_disable = dw_hdmi_qp_bridge_atomic_disable,
	.detect = dw_hdmi_qp_bridge_detect,
	.edid_read = dw_hdmi_qp_bridge_edid_read,
	.hdmi_clear_infoframe = dw_hdmi_qp_bridge_clear_infoframe,
	.hdmi_write_infoframe = dw_hdmi_qp_bridge_write_infoframe,
};

static irqreturn_t dw_hdmi_qp_main_hardirq(int irq, void *dev_id)
{
	struct dw_hdmi_qp *hdmi = dev_id;
	struct dw_hdmi_qp_i2c *i2c = hdmi->i2c;
	u32 stat;

	stat = dw_hdmi_qp_read(hdmi, MAINUNIT_1_INT_STATUS);

	i2c->stat = stat & (I2CM_OP_DONE_IRQ | I2CM_READ_REQUEST_IRQ |
			    I2CM_NACK_RCVD_IRQ);

	if (i2c->stat) {
		dw_hdmi_qp_write(hdmi, i2c->stat, MAINUNIT_1_INT_CLEAR);
		complete(&i2c->cmp);
	}

	if (stat)
		return IRQ_HANDLED;

	return IRQ_NONE;
}

static const struct regmap_config dw_hdmi_qp_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= EARCRX_1_INT_FORCE,
};

static void dw_hdmi_qp_init_hw(struct dw_hdmi_qp *hdmi)
{
	dw_hdmi_qp_write(hdmi, 0, MAINUNIT_0_INT_MASK_N);
	dw_hdmi_qp_write(hdmi, 0, MAINUNIT_1_INT_MASK_N);
	dw_hdmi_qp_write(hdmi, 428571429, TIMER_BASE_CONFIG0);

	/* Software reset */
	dw_hdmi_qp_write(hdmi, 0x01, I2CM_CONTROL0);

	dw_hdmi_qp_write(hdmi, 0x085c085c, I2CM_FM_SCL_CONFIG0);

	dw_hdmi_qp_mod(hdmi, 0, I2CM_FM_EN, I2CM_INTERFACE_CONTROL0);

	/* Clear DONE and ERROR interrupts */
	dw_hdmi_qp_write(hdmi, I2CM_OP_DONE_CLEAR | I2CM_NACK_RCVD_CLEAR,
			 MAINUNIT_1_INT_CLEAR);

	if (hdmi->phy.ops->setup_hpd)
		hdmi->phy.ops->setup_hpd(hdmi, hdmi->phy.data);
}

struct dw_hdmi_qp *dw_hdmi_qp_bind(struct platform_device *pdev,
				   struct drm_encoder *encoder,
				   const struct dw_hdmi_qp_plat_data *plat_data)
{
	static const char * const clk_names[] = {
		"pclk", "earc", "aud", "ref" /* keep "ref" last */
	};
	struct device *dev = &pdev->dev;
	struct dw_hdmi_qp *hdmi;
	void __iomem *regs;
	struct clk *clk;
	int irq, ret, i;

	if (!plat_data->phy_ops || !plat_data->phy_ops->init ||
	    !plat_data->phy_ops->disable || !plat_data->phy_ops->read_hpd) {
		dev_err(dev, "Missing platform PHY ops\n");
		return ERR_PTR(-ENODEV);
	}

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return ERR_PTR(-ENOMEM);

	hdmi->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return regs;

	hdmi->regm = devm_regmap_init_mmio(dev, regs, &dw_hdmi_qp_regmap_config);
	if (IS_ERR(hdmi->regm)) {
		dev_err(dev, "Failed to configure regmap\n");
		return ERR_CAST(hdmi->regm);
	}

	for (i = 0; i < ARRAY_SIZE(clk_names); i++) {
		clk = devm_clk_get_enabled(hdmi->dev, clk_names[i]);

		if (IS_ERR(clk)) {
			if (PTR_ERR(clk) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock\n",
					clk_names[i]);
			return ERR_CAST(clk);
		}
	}
	hdmi->ref_clk = clk;

	hdmi->phy.ops = plat_data->phy_ops;
	hdmi->phy.data = plat_data->phy_data;

	dw_hdmi_qp_init_hw(hdmi);

	irq = platform_get_irq_byname(pdev, "main");
	if (irq < 0)
		return ERR_PTR(irq);

	ret = devm_request_threaded_irq(dev, irq,
					dw_hdmi_qp_main_hardirq, NULL,
					IRQF_SHARED, dev_name(dev), hdmi);
	if (ret)
		return ERR_PTR(ret);

	hdmi->bridge.driver_private = hdmi;
	hdmi->bridge.funcs = &dw_hdmi_qp_bridge_funcs;
	hdmi->bridge.ops = DRM_BRIDGE_OP_DETECT |
			   DRM_BRIDGE_OP_EDID |
			   DRM_BRIDGE_OP_HDMI |
			   DRM_BRIDGE_OP_HPD;
	hdmi->bridge.of_node = pdev->dev.of_node;
	hdmi->bridge.type = DRM_MODE_CONNECTOR_HDMIA;
	hdmi->bridge.vendor = "Synopsys";
	hdmi->bridge.product = "DW HDMI QP TX";

	hdmi->bridge.ddc = dw_hdmi_qp_i2c_adapter(hdmi);
	if (IS_ERR(hdmi->bridge.ddc))
		hdmi->bridge.ddc = NULL;

	ret = devm_drm_bridge_add(dev, &hdmi->bridge);
	if (ret)
		return ERR_PTR(ret);

	ret = drm_bridge_attach(encoder, &hdmi->bridge, NULL,
				DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret)
		return ERR_PTR(ret);

	return hdmi;
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_bind);

void dw_hdmi_qp_resume(struct device *dev, struct dw_hdmi_qp *hdmi)
{
	dw_hdmi_qp_init_hw(hdmi);
}
EXPORT_SYMBOL_GPL(dw_hdmi_qp_resume);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_AUTHOR("Cristian Ciocaltea <cristian.ciocaltea@collabora.com>");
MODULE_DESCRIPTION("DW HDMI QP transmitter library");
MODULE_LICENSE("GPL");
