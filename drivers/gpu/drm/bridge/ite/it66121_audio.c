// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Heiko Stuebner <heiko@sntech.de>
 *
 * based on beagleboard it66121 i2c encoder driver
 * Copyright (C) 2017 Baozhu Zuo <zuobaozhu@gmail.com>
 */
#include <sound/asoundef.h>
#include <sound/hdmi-codec.h>

#include "it66121.h"

/* FIXME: handline in the driver */
#define M_TX_AUD_BIT M_TX_AUD_16BIT

#define SUPPORT_AUDI_AudSWL 16
#if(SUPPORT_AUDI_AudSWL==16)
    #define CHTSTS_SWCODE 0x02
#elif(SUPPORT_AUDI_AudSWL==18)
    #define CHTSTS_SWCODE 0x04
#elif(SUPPORT_AUDI_AudSWL==20)
    #define CHTSTS_SWCODE 0x03
#else
    #define CHTSTS_SWCODE 0x0B
#endif


static int it66121_aud_config_aai(struct it66121 *priv, struct hdmi_codec_params *params)
{
	struct hdmi_audio_infoframe *infoframe = &params->cea;
	u8 buf[HDMI_INFOFRAME_SIZE(AUDIO)];
	int ret;

	ret = hdmi_audio_infoframe_pack(infoframe, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	ret = it66121_reg_bulk_write(priv, IT66121_AUDINFO_CC,
				     buf + HDMI_INFOFRAME_HEADER_SIZE, 5);
	if (ret < 0) {
		DRM_ERROR("failed to write AVI infoframe: %d\n", ret);
		return;
	}

	ret = it66121_reg_write(priv, IT66121_AUDINFO_SUM, buf[3]);
	if (ret < 0) {
		DRM_ERROR("failed to write AVI infoframe: %d\n", ret);
		return;
	}

	it66121_reg_write(priv, IT66121_AUD_INFOFRM_CTRL, IT66121_INFOFRM_ENABLE_PACKET | IT66121_INFOFRM_REPEAT_PACKET);
}

static void it66121_aud_set_fs(struct it66121 *priv, u8 fs)
{
	u32 n;
	u32  LastCTS = 0;
	u8 HBR_mode;
	u8 udata;

	if (IT66121_AUD_HD_HBR & it66121_reg_read(priv, IT66121_AUD_HD))
		HBR_mode = 1;
	else
		HBR_mode = 0;

	printk("HBR_mode:%d\n", HBR_mode);
	switch (fs) {
	case IEC958_AES3_CON_FS_32000:
		n = 4096; break;
	case IEC958_AES3_CON_FS_44100:
		n = 6272; break;
	case IEC958_AES3_CON_FS_48000:
		n = 6144; break;
	case IEC958_AES3_CON_FS_88200:
		n = 12544; break;
	case IEC958_AES3_CON_FS_96000:
		n = 12288; break;
	case IEC958_AES3_CON_FS_176400:
		n = 25088; break;
	case IEC958_AES3_CON_FS_192000:
		n = 24576; break;
	case IEC958_AES3_CON_FS_768000:
		n = 24576; break;
	default:
		n = 6144;
	}

	it66121_reg_write(priv, IT66121_PKT_AUD_N0, (u8)((n)&0xFF));
	it66121_reg_write(priv, IT66121_PKT_AUD_N1, (u8)((n >> 8) & 0xFF));
	it66121_reg_write(priv, IT66121_PKT_AUD_N2, (u8)((n >> 16) & 0xF));

	it66121_reg_write(priv, IT66121_PKT_AUD_CTS0, (u8)((LastCTS)&0xFF));
	it66121_reg_write(priv, IT66121_PKT_AUD_CTS1, (u8)((LastCTS >> 8) & 0xFF));
	it66121_reg_write(priv, IT66121_PKT_AUD_CTS2, (u8)((LastCTS >> 16) & 0xF));

	it66121_reg_write(priv, 0xF8, 0xC3);
	it66121_reg_write(priv, 0xF8, 0xA5);


	udata =  it66121_reg_read(priv, IT66121_SINGLE_CTRL);
	udata &= ~IT66121_SINGLE_CTRL_AUDIO_CTS_USER;
	it66121_reg_write(priv, IT66121_SINGLE_CTRL, udata);

	it66121_reg_write(priv, 0xF8, 0xFF);

	if (0 == HBR_mode) { //LPCM
		fs = IEC958_AES3_CON_FS_768000;
		it66121_reg_write(priv, IT66121_AUDCHST_CA_FS, 0x00 | fs);
		fs = ~fs; // OFS is the one's complement of FS
		udata = (0x0f & it66121_reg_read(priv, IT66121_AUDCHST_OFS_WL));
		it66121_reg_write(priv, IT66121_AUDCHST_OFS_WL, (fs << 4) | udata);
	}
}

static void it66121_set_ChStat(struct it66121 *priv, u8 ucIEC60958ChStat[])
{
	u8 udata;

	udata = (ucIEC60958ChStat[0] << 1) & 0x7C;
	it66121_reg_write(priv, IT66121_AUDCHST_MODE, udata);
	it66121_reg_write(priv, IT66121_AUDCHST_CAT, ucIEC60958ChStat[1]); // 192, audio CATEGORY
	it66121_reg_write(priv, IT66121_AUDCHST_SRCNUM, ucIEC60958ChStat[2] & 0xF);
	it66121_reg_write(priv, IT66121_AUD0CHST_CHTNUM, (ucIEC60958ChStat[2] >> 4) & 0xF);
	it66121_reg_write(priv, IT66121_AUDCHST_CA_FS, ucIEC60958ChStat[3]); // choose clock
	it66121_reg_write(priv, IT66121_AUDCHST_OFS_WL, ucIEC60958ChStat[4]);
}

static void it66121_set_HBRAudio(struct it66121 *priv)
{
	u8 udata;

	it66121_reg_write(priv, IT66121_AUDIO_CTRL1, 0x47); // regE1 bOutputAudioMode should be loaded from ROM image.
	it66121_reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT | B_TX_AUD_SPDIF);
		it66121_reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
	} else {
		it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT);
		it66121_reg_write(priv, IT66121_AUDIO_CTRL3, 0);
	}
	it66121_reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x08);
	it66121_reg_write(priv, IT66121_AUD_HD, IT66121_AUD_HD_HBR); // regE5 = 0 ;

	//uc = HDMITX_ReadI2C_Byte(client,IT66121_CLK_CTRL1);
	//uc &= ~M_TX_AUD_DIV ;
	//HDMITX_WriteI2C_Byte(client,IT66121_CLK_CTRL1, uc);

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		u8 i;
		for (i = 0; i < 100; i++) {
			if (it66121_reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
		it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
				  B_TX_AUD_SPDIF | B_TX_AUD_EN_SPDIF);
	} else {
		it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
				  B_TX_AUD_EN_I2S3 |
				  B_TX_AUD_EN_I2S2 |
				  B_TX_AUD_EN_I2S1 |
				  B_TX_AUD_EN_I2S0);
	}
	udata = it66121_reg_read(priv, 0x5c);
	udata &= ~(1 << 6);
	it66121_reg_write(priv, 0x5c, udata);

	//hdmiTxDev[0].bAudioChannelEnable = it66121_reg_read(priv, IT66121_AUDIO_CTRL0);
	// it66121_reg_write(priv,IT66121_SW_RST, rst  );
}

static void it66121_set_DSDAudio(struct it66121 *priv)
{
	// to be continue
	// u8 rst;
	// rst = it66121_reg_read(priv,IT66121_SW_RST);

	//red_write(client,IT66121_SW_RST, rst | (B_HDMITX_AUD_RST|B_TX_AREF_RST) );

	it66121_reg_write(priv, IT66121_AUDIO_CTRL1, 0x41); // regE1 bOutputAudioMode should be loaded from ROM image.
	it66121_reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT);
	it66121_reg_write(priv, IT66121_AUDIO_CTRL3, 0);

	it66121_reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	it66121_reg_write(priv, IT66121_AUD_HD, IT66121_AUD_HD_DSD); // regE5 = 0 ;
													 //red_write(client,IT66121_SW_RST, rst & ~(B_HDMITX_AUD_RST|B_TX_AREF_RST) );

	//uc = it66121_reg_read(priv,IT66121_CLK_CTRL1);
	//uc &= ~M_TX_AUD_DIV ;
	//red_write(client,IT66121_CLK_CTRL1, uc);

	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, M_TX_AUD_BIT |
			  B_TX_AUD_EN_I2S3 |
			  B_TX_AUD_EN_I2S2 |
			  B_TX_AUD_EN_I2S1 |
			  B_TX_AUD_EN_I2S0);
}

static void it66121_set_NLPCMAudio(struct it66121 *priv)
{ // no Source Num, no I2S.
	u8 AudioEnable, AudioFormat;
	u8 i;
	AudioFormat = 0x01; // NLPCM must use standard I2S mode.
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		AudioEnable = M_TX_AUD_BIT | B_TX_AUD_SPDIF;
	} else {
		AudioEnable = M_TX_AUD_BIT;
	}

	// HDMITX_WriteI2C_Byte(client,IT66121_AUDIO_CTRL0, M_TX_AUD_24BIT|B_TX_AUD_SPDIF);
	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable);
	//HDMITX_AndREG_Byte(IT66121_SW_RST,~(B_HDMITX_AUD_RST|B_TX_AREF_RST));

	it66121_reg_write(priv, IT66121_AUDIO_CTRL1, 0x01); // regE1 bOutputAudioMode should be loaded from ROM image.
	it66121_reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.

#ifdef USE_SPDIF_CHSTAT
	it66121_reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
#else // not USE_SPDIF_CHSTAT
	it66121_reg_write(priv, IT66121_AUDIO_CTRL3, 0);
#endif // USE_SPDIF_CHSTAT

	it66121_reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	it66121_reg_write(priv, IT66121_AUD_HD, 0x00); // regE5 = 0 ;

	if (CONFIG_INPUT_AUDIO_SPDIF) {
		for (i = 0; i < 100; i++) {
			if (it66121_reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
	}
	priv->AudioChannelEnable = AudioEnable;
	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable | B_TX_AUD_EN_I2S0);
}

static void it66121_set_LPCMAudio(struct it66121 *priv,
				  u8 AudioSrcNum, u8 AudSWL)
{
	u8 AudioEnable, AudioFormat;

	AudioEnable = 0;
	AudioFormat = 0;

	switch (AudSWL) {
	case 16:
		AudioEnable |= M_TX_AUD_16BIT;
		break;
	case 18:
		AudioEnable |= M_TX_AUD_18BIT;
		break;
	case 20:
		AudioEnable |= M_TX_AUD_20BIT;
		break;
	case 24:
	default:
		AudioEnable |= M_TX_AUD_24BIT;
		break;
	}
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		AudioFormat &= ~0x40;
		AudioEnable |= B_TX_AUD_SPDIF | B_TX_AUD_EN_I2S0;
	} else {
		AudioFormat |= 0x40;
		switch (AudioSrcNum) {
		case 4:
			AudioEnable |= B_TX_AUD_EN_I2S3 | B_TX_AUD_EN_I2S2 | B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 3:
			AudioEnable |= B_TX_AUD_EN_I2S2 | B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 2:
			AudioEnable |= B_TX_AUD_EN_I2S1 | B_TX_AUD_EN_I2S0;
			break;

		case 1:
		default:
			AudioFormat &= ~0x40;
			AudioEnable |= B_TX_AUD_EN_I2S0;
			break;

		}
	}

	if (AudSWL != 16)
		AudioFormat |= 0x01;

	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, AudioEnable & 0xF0);

	// regE1 bOutputAudioMode should be loaded from ROM image.
	it66121_reg_write(priv, IT66121_AUDIO_CTRL1,
			  AudioFormat |
			  B_TX_AUDFMT_DELAY_1T_TO_WS |
			  B_TX_AUDFMT_RISE_EDGE_SAMPLE_WS
			 );


	it66121_reg_write(priv, IT66121_AUDIO_FIFOMAP, 0xE4); // default mapping.
#ifdef USE_SPDIF_CHSTAT
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		it66121_reg_write(priv, IT66121_AUDIO_CTRL3, B_TX_CHSTSEL);
	} else {
		it66121_reg_write(priv, IT66121_AUDIO_CTRL3, 0);
	}
#else // not USE_SPDIF_CHSTAT
	it66121_reg_write(priv, IT66121_AUDIO_CTRL3, 0);
#endif // USE_SPDIF_CHSTAT

	it66121_reg_write(priv, IT66121_AUD_SRCVALID_FLAT, 0x00);
	it66121_reg_write(priv, IT66121_AUD_HD, 0x00); // regE5 = 0 ;
	priv->AudioChannelEnable = AudioEnable;
	if (CONFIG_INPUT_AUDIO_SPDIF) {
		u8 i;
		it66121_reg_update_bits(priv, 0x5c, (1 << 6), (1 << 6));
		for (i = 0; i < 100; i++) {
			if (it66121_reg_read(priv, IT66121_CLK_STATUS1) & IT66121_CLK_STATUS1_OSF_LOCK) {
				break; // stable clock.
			}
		}
	}
}

static int it66121_aud_output_config(struct it66121 *priv,
				     struct hdmi_codec_params *param)
{
	u8 udata;
	u8 fs;
	u8 ucIEC60958ChStat[8];

	it66121_reg_update_bits(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD),
				  (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD));
	it66121_reg_write(priv, IT66121_CLK_CTRL0, IT66121_CLK_CTRL0_OSCLK_AUTO | IT66121_CLK_CTRL0_MCLK_256FS | IT66121_CLK_CTRL0_IPCLK_AUTO);

	it66121_reg_update_bits(priv, IT66121_SYS_STATUS1, 0x10, 0x00); // power on the ACLK

	//use i2s
	udata = it66121_reg_read(priv, IT66121_AUDIO_CTRL0);
	udata &= ~B_TX_AUD_SPDIF;
	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, udata);


	// one bit audio have no channel status.
	switch (param->sample_rate) {
	case 32000:
		fs = IEC958_AES3_CON_FS_32000;
		break;
	case 44100:
		fs = IEC958_AES3_CON_FS_44100;
		break;
	case 48000:
		fs = IEC958_AES3_CON_FS_48000;
		break;
	case 88200:
		fs = IEC958_AES3_CON_FS_88200;
		break;
	case 96000:
		fs = IEC958_AES3_CON_FS_96000;
		break;
	case 176400:
		fs = IEC958_AES3_CON_FS_176400;
		break;
	case 192000:
		fs = IEC958_AES3_CON_FS_192000;
		break;
	case 768000:
		fs = IEC958_AES3_CON_FS_768000;
		break;
	default:
		fs = IEC958_AES3_CON_FS_48000;
		break;
	}
	it66121_aud_set_fs(priv, fs);

	ucIEC60958ChStat[0] = 0;
	ucIEC60958ChStat[1] = 0;
	ucIEC60958ChStat[2] = (param->channels + 1) / 2;

	if (ucIEC60958ChStat[2] < 1) {
		ucIEC60958ChStat[2] = 1;
	} else if (ucIEC60958ChStat[2] > 4) {
		ucIEC60958ChStat[2] = 4;
	}
	ucIEC60958ChStat[3] = fs;
	ucIEC60958ChStat[4] = (((~fs) << 4) & 0xF0) | CHTSTS_SWCODE; // Fs | 24bit word length

	it66121_reg_update_bits(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), IT66121_SW_RST_SOFT_AUD);

	switch (CNOFIG_INPUT_AUDIO_TYPE) {
	case T_AUDIO_HBR:
		ucIEC60958ChStat[0] |= 1 << 1;
		ucIEC60958ChStat[2] = 0;
		ucIEC60958ChStat[3] &= 0xF0;
		ucIEC60958ChStat[3] |= IEC958_AES3_CON_FS_768000;
		ucIEC60958ChStat[4] |= (((~IEC958_AES3_CON_FS_768000) << 4) & 0xF0) | 0xB;
		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_HBRAudio(priv);

		break;
	case T_AUDIO_DSD:
		it66121_set_DSDAudio(priv);
		break;
	case T_AUDIO_NLPCM:
		ucIEC60958ChStat[0] |= 1 << 1;
		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_NLPCMAudio(priv);
		break;
	case T_AUDIO_LPCM:
		ucIEC60958ChStat[0] &= ~(1 << 1);

		it66121_set_ChStat(priv, ucIEC60958ChStat);
		it66121_set_LPCMAudio(priv, (param->channels + 1) / 2, SUPPORT_AUDI_AudSWL);
		// can add auto adjust
		break;
	}
	it66121_reg_update_bits(priv, IT66121_INT_MASK0, IT66121_INT_MASK0_AUDIO_OVERFLOW, 0);
	it66121_reg_write(priv, IT66121_AUDIO_CTRL0, priv->AudioChannelEnable);
	it66121_reg_update_bits(priv, IT66121_SW_RST, (IT66121_SW_RST_AUDIO_FIFO | IT66121_SW_RST_SOFT_AUD), 0);
	return 0;
}

static int it66121_audio_hw_params(struct device *dev, void *data,
				   struct hdmi_codec_daifmt *daifmt,
				   struct hdmi_codec_params *params)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	dev_err(&priv->i2c->dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
			params->sample_rate, params->sample_width, params->channels);

	it66121_aud_config_aai(priv, params);
	it66121_aud_output_config(priv, params);
	return 0;
}

static void it66121_audio_shutdown(struct device *dev, void *data)
{
}

static int it66121_audio_digital_mute(struct device *dev, void *data, bool enable)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	return 0;
}

static int it66121_audio_get_eld(struct device *dev, void *data,
				 uint8_t *buf, size_t len)
{
	struct it66121 *priv = dev_get_drvdata(dev);

	memcpy(buf, priv->connector.eld, min(sizeof(priv->connector.eld), len));

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = it66121_audio_hw_params,
	.audio_shutdown = it66121_audio_shutdown,
	.digital_mute = it66121_audio_digital_mute,
	.get_eld = it66121_audio_get_eld,
};

static const struct hdmi_codec_pdata codec_data = {
	.ops = &audio_codec_ops,
	.max_i2s_channels = 2,
	.i2s = 1,
};

// #define INV_INPUT_ACLK

#ifndef INV_INPUT_ACLK
#define InvAudCLK 0
#else
#define InvAudCLK B_TX_AUDFMT_FALL_EDGE_SAMPLE_WS
#endif

int it66121_audio_init(struct device *dev, struct it66121 *priv)
{
	int ret;

	ret = it66121_reg_update_bits(priv, IT66121_AUDIO_CTRL1, 0x20, InvAudCLK);
	if (ret < 0)
		return ret;

	priv->audio_pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(priv->audio_pdev);
}

void it66121_audio_exit(struct it66121 *priv)
{
	if (priv->audio_pdev)
		platform_device_unregister(priv->audio_pdev);
}
