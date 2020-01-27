// SPDX-License-Identifier: GPL-2.0-only
/*
 * vcnl3020.c - Support for Vishay VCNL3020 proximity sensor
 *
 * based on vcnl4000.c
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/platform_device.h>
#include <linux/iio/proximity/vcnl3020.h>

static const struct i2c_device_id vcnl3020_id[] = {
	{ "vcnl3020", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, vcnl3020_id);

static int32_t vcnl3020_init(struct vcnl3020_data *data)
{
	s32 rc;
	u32 proximity_rate, led_current, threshold, count_exceed, param_val;

	struct device *dev = &data->client->dev;

	rc = device_property_read_u32(dev, "proximity-rate", &param_val);
	if (rc) {
		dev_err(dev, "couldn't get proximity rate %x", rc);
		goto exit;
	}
	proximity_rate = param_val;

	rc = device_property_read_u32(dev, "led-current", &param_val);
	if (rc) {
		dev_err(dev, "Couldn't get led current %x", rc);
		goto exit;
	}
	led_current = param_val;

	rc = device_property_read_u32(dev, "threshold", &param_val);
	if (rc) {
		dev_err(dev, "Couldn't get threshold %x", rc);
		goto exit;
	}
	threshold = param_val;

	rc = device_property_read_u32(dev, "count-exceed", &param_val);
	if (rc) {
		dev_err(dev, "couldn't get count exceed %x", rc);
		goto exit;
	}
	count_exceed = param_val;

	rc = i2c_smbus_read_byte_data(data->client, VCNL_PROD_REV);
	if (rc < 0) {
		dev_err(dev, "Error in prod rev reading out %x", rc);
		goto exit;
	}

	if (rc == VCNL3020_PROD_ID) {
		data->rev = rc & 0xff;
		mutex_init(&data->vcnl3020_lock);
	} else {
		dev_err(dev, "Prod id is not related to vcnl3020");
		rc = -ENODEV;
		goto exit;
	}

	/* set proximity rate */
	rc = i2c_smbus_write_byte_data(data->client, VCNL_PROXIMITY_RATE,
				       proximity_rate);
	if (rc < 0) {
		dev_err(dev, "Error set proximity rate %x", rc);
		goto exit;
	}

	/* set led current */
	rc = i2c_smbus_write_byte_data(data->client, VCNL_LED_CURRENT,
				       led_current);
	if (rc < 0) {
		dev_err(dev, "Error set led current %x", rc);
		goto exit;
	}

	/* set high threshold value */
	rc = i2c_smbus_write_byte_data(data->client, VCNL_PS_LO_THR_LO,
				       (threshold) & 0xFF);
	if (rc < 0) {
		dev_err(dev, "Error set high threshold lreg %x", rc);
		goto exit;
	}

	/* set low threshold value */
	rc = i2c_smbus_write_byte_data(data->client, VCNL_PS_LO_THR_HI,
				       (threshold >> 8) & 0xFF);
	if (rc < 0) {
		dev_err(dev, "Error set high threshold hreg %x", rc);
		goto exit;
	}

	/* enable interrupt for exceeding low/high thresholds */
	rc = i2c_smbus_write_byte_data(data->client, VCNL_PS_ICR,
				       (count_exceed << ICR_COUNT_EXCEED) |
				       ICR_THRES_EN);
	if (rc < 0) {
		dev_err(dev, "Error set interrupt control reg %x", rc);
		goto exit;
	}

	rc = i2c_smbus_write_byte_data(data->client, VCNL_COMMAND,
				       (VCNL_PS_EN | VCNL_PS_SELFTIMED_EN) &
				       (VCNL_PS_OD ^ 0xff));
	if (rc < 0) {
		dev_err(dev, "Error set interrupt control reg %x", rc);
		goto exit;
	}

	rc = i2c_smbus_read_byte_data(data->client, VCNL_COMMAND);
	dev_err(dev, "command data %x", rc);

	return 0;
exit:
	return rc;
};

#ifdef CONFIG_SENSORS_VCNL3020

bool vcnl3020_intrusion(struct vcnl3020_data *data)
{
	s32 isr;

	mutex_lock(&data->vcnl3020_lock);
	isr = i2c_smbus_read_byte_data(data->client, VCNL_ISR);
	if (isr < 0) {
		dev_err(&data->client->dev, "Error reading interrupt status %x",
			isr);
	}
	mutex_unlock(&data->vcnl3020_lock);

	return !!(isr & BIT(INT_TH_LOW));
}
EXPORT_SYMBOL_GPL(vcnl3020_read_isr);

int32_t vcnl3020_clear_interrupts(struct vcnl3020_data *data)
{
	s32 rc;

	mutex_lock(&data->vcnl3020_lock);
	rc = i2c_smbus_write_byte_data(data->client, VCNL_ISR,
				       INT_TH_HI | INT_TH_LOW | INT_PROX_READY);
	mutex_unlock(&data->vcnl3020_lock);

	return rc;
}
EXPORT_SYMBOL_GPL(vcnl3020_clear_interrupts);

#endif

static int32_t vcnl3020_measure_proximity(struct vcnl3020_data *data,
					  int32_t *val)
{
	s32 tries = 20;
	s32 rc = 0;
	s32 cmdreg = 0;

	mutex_lock(&data->vcnl3020_lock);
	/* store command register state before measurement */
	cmdreg = i2c_smbus_read_byte_data(data->client, VCNL_COMMAND);
	if (cmdreg < 0)
		goto fail;

	rc = i2c_smbus_write_byte_data(data->client, VCNL_COMMAND,
				       VCNL_PS_OD);
	if (rc < 0)
		goto fail;

	/* wait for data to become ready */
	while (tries--) {
		rc = i2c_smbus_read_byte_data(data->client, VCNL_COMMAND);
		if (rc < 0)
			goto fail;
		if (rc & VCNL_PS_RDY)
			break;
		msleep(20); /* measurement takes up to 100 ms */
	}

	if (tries < 0) {
		dev_err(&data->client->dev,
			"vcnl3020_measure() failed, data is not ready");
		rc = -EIO;
		goto fail;
	}

	rc = i2c_smbus_read_byte_data(data->client, VCNL_PS_RESULT_HI);
	if (rc < 0)
		goto fail;
	*val = (rc & 0xff) << 8;
	dev_dbg(&data->client->dev, "result high byte 0x%x", rc);

	rc = i2c_smbus_read_byte_data(data->client, VCNL_PS_RESULT_LO);
	if (rc < 0)
		goto fail;
	*val |= rc & 0xff;
	dev_dbg(&data->client->dev, "result low byte 0x%x", rc);

	rc = i2c_smbus_write_byte_data(data->client, VCNL_COMMAND, cmdreg);
	if (rc < 0)
		goto fail;

	mutex_unlock(&data->vcnl3020_lock);

	return 0;
fail:
	mutex_unlock(&data->vcnl3020_lock);
	return rc;
}

static const struct iio_chan_spec vcnl3020_channels[] = {
	{
		.type = IIO_PROXIMITY,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	}
};

static int32_t vcnl3020_read_raw(struct iio_dev *indio_dev,
				 struct iio_chan_spec const *chan,
				 s32 *val, s32 *val2, long mask)
{
	s32 rc;
	struct vcnl3020_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_PROXIMITY:
			rc = vcnl3020_measure_proximity(data, val);
			if (rc < 0)
				return rc;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_info vcnl3020_info = {
	.read_raw = vcnl3020_read_raw,
};

static int32_t vcnl3020_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct vcnl3020_data *data;
	struct iio_dev *indio_dev;
	struct platform_device *pdev;
	s32 rc;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;

	rc = vcnl3020_init(data);
	if (rc < 0)
		goto out;

	dev_info(&client->dev, "Proximity sensor, Rev: %02x\n",
		 VCNL3020_PROD_ID);

	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &vcnl3020_info;
	indio_dev->channels = vcnl3020_channels;
	indio_dev->num_channels = ARRAY_SIZE(vcnl3020_channels);
	indio_dev->name = VCNL_DRV_NAME;
	indio_dev->modes = INDIO_DIRECT_MODE;

	rc = devm_iio_device_register(&client->dev, indio_dev);
	if (rc != 0)
		goto out;

#ifdef CONFIG_SENSORS_VCNL3020
	pdev = platform_device_alloc(VCNL_DRV_HWMON, -1);
	if (!pdev) {
		dev_err(&client->dev, "Failed to allocate %s\n",
			VCNL_DRV_HWMON);
		rc = -ENOMEM;
		goto out;
	}

	pdev->dev.parent = &indio_dev->dev;
	platform_set_drvdata(pdev, data);
	rc = platform_device_add(pdev);
	if (rc != 0) {
		dev_err(&client->dev, "Failed to register %s: %d\n",
			VCNL_DRV_HWMON, rc);
		platform_device_put(pdev);
		pdev = NULL;
		goto out;
	}

#endif

	return rc;
out:
	kfree(indio_dev);
	return rc;
}

static const struct of_device_id vcnl3020_of_match[] = {
	{
		.compatible = "vishay,vcnl3020",
	},
};
MODULE_DEVICE_TABLE(of, vcnl3020_of_match);

static struct i2c_driver vcnl3020_driver = {
	.driver = {
		.name   = VCNL_DRV_NAME,
		.of_match_table = vcnl3020_of_match,
	},
	.probe  = vcnl3020_probe,
	.id_table = vcnl3020_id,
};
module_i2c_driver(vcnl3020_driver);

MODULE_AUTHOR("Ivan Mikhaylov <i.mikhaylov@yadro.com>");
MODULE_DESCRIPTION("Vishay VCNL3020 proximity sensor driver");
MODULE_LICENSE("GPL");
