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

static void it66121_aud_config_aai(struct it66121 *priv)
{
	u8 aud_db[AUDIO_INFOFRAME_LEN];
	unsigned int checksum = 0;
	u8 i;

//FIXME check and use hdmi_audio_infoframe_pack
	aud_db[0] = 1;

	for (i = 1; i < AUDIO_INFOFRAME_LEN; i++) {
		aud_db[i] = 0;
	}

	checksum = 0x100 - (AUDIO_INFOFRAME_VER + AUDIO_INFOFRAME_TYPE + AUDIO_INFOFRAME_LEN);
	it66121_reg_write(priv, IT66121_AUDINFO_CC, aud_db[0]);
	checksum -= it66121_reg_read(priv, IT66121_AUDINFO_CC);
	checksum &= 0xFF;

	it66121_reg_write(priv, IT66121_AUDINFO_SF, aud_db[1]);
	checksum -= it66121_reg_read(priv, IT66121_AUDINFO_SF);
	checksum &= 0xFF;

	it66121_reg_write(priv, IT66121_AUDINFO_CA, aud_db[3]);
	checksum -= it66121_reg_read(priv, IT66121_AUDINFO_CA);
	checksum &= 0xFF;

	it66121_reg_write(priv, IT66121_AUDINFO_DM_LSV, aud_db[4]);
	checksum -= it66121_reg_read(priv, IT66121_AUDINFO_DM_LSV);
	checksum &= 0xFF;

	it66121_reg_write(priv, IT66121_AUDINFO_SUM, checksum);

	it66121_reg_write(priv, IT66121_AUD_INFOFRM_CTRL, IT66121_INFOFRM_ENABLE_PACKET | IT66121_INFOFRM_REPEAT_PACKET);
}

static void it66121_aud_set_fs(struct it66121 *priv, u8 fs)
{
	u32 n;
	u32  LastCTS = 0;
	u8 HBR_mode;
	u8 udata;

	if (B_TX_HBR & it66121_reg_read(priv, IT66121_AUD_HDAUDIO))
		HBR_mode = 1;
	else
		HBR_mode = 0;

	printk("HBR_mode:%d\n", HBR_mode);
	switch (fs) {
	case AUDFS_32KHz:
		n = 4096; break;
	case AUDFS_44p1KHz:
		n = 6272; break;
	case AUDFS_48KHz:
		n = 6144; break;
	case AUDFS_88p2KHz:
		n = 12544; break;
	case AUDFS_96KHz:
		n = 12288; break;
	case AUDFS_176p4KHz:
		n = 25088; break;
	case AUDFS_192KHz:
		n = 24576; break;
	case AUDFS_768KHz:
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
		fs = AUDFS_768KHz;
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
	it66121_reg_write(priv, IT66121_AUD_HDAUDIO, B_TX_HBR); // regE5 = 0 ;

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
	it66121_reg_write(priv, IT66121_AUD_HDAUDIO, B_TX_DSD); // regE5 = 0 ;
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
	it66121_reg_write(priv, IT66121_AUD_HDAUDIO, 0x00); // regE5 = 0 ;

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
	it66121_reg_write(priv, IT66121_AUD_HDAUDIO, 0x00); // regE5 = 0 ;
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
	case  44100L:
		fs =  AUDFS_44p1KHz; break;
	case  88200L:
		fs =  AUDFS_88p2KHz; break;
	case 176400L:
		fs = AUDFS_176p4KHz; break;
	case  32000L:
		fs =    AUDFS_32KHz; break;
	case  48000L:
		fs =    AUDFS_48KHz; break;
	case  96000L:
		fs =    AUDFS_96KHz; break;
	case 192000L:
		fs =   AUDFS_192KHz; break;
	case 768000L:
		fs =   AUDFS_768KHz; break;
	default:
		//SampleFreq = 48000L;
		fs =    AUDFS_48KHz;
		break; // default, set Fs = 48KHz.
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
		ucIEC60958ChStat[3] |= AUDFS_768KHz;
		ucIEC60958ChStat[4] |= (((~AUDFS_768KHz) << 4) & 0xF0) | 0xB;
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
	udata = it66121_reg_read(priv, IT66121_INT_MASK1);
	udata &= ~IT66121_INT_MASK1_AUDIO_OVERFLOW;
	it66121_reg_write(priv, IT66121_INT_MASK1, udata);
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

	it66121_aud_config_aai(priv);
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

int it66121_audio_init(struct device *dev, struct it66121 *priv)
{
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
