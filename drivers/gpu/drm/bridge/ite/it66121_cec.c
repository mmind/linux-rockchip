#include <media/cec.h>

#include "it66121.h"

static int it66121_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct it66121 *priv = cec_get_drvdata(adap);



	if (!priv->cec_enabled_adap && enable) {

		it66121_reg_update_bits(priv, IT66121_SYS_STATUS1,
					IT66121_SYS_STATUS1_GATE_CRCLK, 0);

//unmask irqs


	} else if (priv->cec_enabled_adap && !enable) {
/////
//		adv7511->cec_valid_addrs = 0;

//mask irqs

		it66121_reg_update_bits(priv, IT66121_SYS_STATUS1,
					IT66121_SYS_STATUS1_GATE_CRCLK,
					IT66121_SYS_STATUS1_GATE_CRCLK);
	}

	priv->cec_enabled_adap = enable;
	return 0;
}

static int it66121_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct it66121 *priv = cec_get_drvdata(adap);
	int i;
//	unsigned int i, free_idx = ADV7511_MAX_ADDRS;

	if (!priv->cec_enabled_adap)
		return addr == CEC_LOG_ADDR_INVALID ? 0 : -EIO;

	if (addr == CEC_LOG_ADDR_INVALID) {
/*		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x70, 0);
		adv7511->cec_valid_addrs = 0;*/
		return 0;
	}

/*	for (i = 0; i < ADV7511_MAX_ADDRS; i++) {
		bool is_valid = adv7511->cec_valid_addrs & (1 << i);

		if (free_idx == ADV7511_MAX_ADDRS && !is_valid)
			free_idx = i;
		if (is_valid && adv7511->cec_addr[i] == addr)
			return 0;
	}
	if (i == ADV7511_MAX_ADDRS) {
		i = free_idx;
		if (i == ADV7511_MAX_ADDRS)
			return -ENXIO;
	}
	adv7511->cec_addr[i] = addr;
	adv7511->cec_valid_addrs |= 1 << i;
*/

	switch (i) {
	case 0:
		/* enable address mask 0 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
//				   0x10, 0x10);
		/* set address for mask 0 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_0_1 + offset,
//				   0x0f, addr);
		break;
	case 1:
		/* enable address mask 1 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
//				   0x20, 0x20);
		/* set address for mask 1 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_0_1 + offset,
//				   0xf0, addr << 4);
		break;
	case 2:
		/* enable address mask 2 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
//				   0x40, 0x40);
		/* set address for mask 1 */
//		regmap_update_bits(adv7511->regmap_cec,
//				   ADV7511_REG_CEC_LOG_ADDR_2 + offset,
//				   0x0f, addr);
		break;
	}
	return 0;
}

static int it66121_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				     u32 signal_free_time, struct cec_msg *msg)
{
	struct it66121 *priv = cec_get_drvdata(adap);
	u8 len = msg->len;
	unsigned int i;

	/*
	 * The number of retries is the number of attempts - 1, but retry
	 * at least once. It's not clear if a value of 0 is allowed, so
	 * let's do at least one retry.
	 */
/*	regmap_update_bits(adv7511->regmap_cec,
			   ADV7511_REG_CEC_TX_RETRY + offset,
			   0x70, max(1, attempts - 1) << 4);*/

	/* blocking, clear cec tx irq status */
//	regmap_update_bits(adv7511->regmap, ADV7511_REG_INT(1), 0x38, 0x38);

	/* write data */
/*	for (i = 0; i < len; i++)
		regmap_write(adv7511->regmap_cec,
			     i + ADV7511_REG_CEC_TX_FRAME_HDR + offset,
			     msg->msg[i]); */

	/* set length (data + header) */
//	regmap_write(adv7511->regmap_cec,
//		     ADV7511_REG_CEC_TX_FRAME_LEN + offset, len);
	/* start transmit, enable tx */
//	regmap_write(adv7511->regmap_cec,
//		     ADV7511_REG_CEC_TX_ENABLE + offset, 0x01);
	return 0;
}

static const struct cec_adap_ops it66121_cec_adap_ops = {
	.adap_enable = it66121_cec_adap_enable,
	.adap_log_addr = it66121_cec_adap_log_addr,
	.adap_transmit = it66121_cec_adap_transmit,
};

void it66121_cec_irq_process(struct it66121 *priv)
{

}

int it66121_cec_init(struct device *dev, struct it66121 *priv)
{
	int ret;

	if (!priv->i2c_cec || !priv->regmap_cec)
		return -ENODEV;

	priv->cec_adap = cec_allocate_adapter(&it66121_cec_adap_ops,
		priv, dev_name(dev), CEC_CAP_DEFAULTS, CEC_MAX_LOG_ADDRS);
	if (IS_ERR(priv->cec_adap)) {
		ret = PTR_ERR(priv->cec_adap);
		goto err_cec_alloc;
	}

	//FIXME: init voodoo

	ret = cec_register_adapter(priv->cec_adap, dev);
	if (ret)
		goto err_cec_register;

	return 0;

err_cec_register:
	cec_delete_adapter(priv->cec_adap);
	priv->cec_adap = NULL;
err_cec_alloc:
/*	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL + offset,
		     ADV7511_CEC_CTRL_POWER_DOWN);*/
	return ret == -EPROBE_DEFER ? ret : 0;
}
