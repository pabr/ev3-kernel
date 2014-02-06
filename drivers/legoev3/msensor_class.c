/*
 * Measurement sensor device class for LEGO Mindstorms EV3
 *
 * Copyright (C) 2013 David Lechner <david@lechnology.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.

 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/legoev3/msensor_class.h>

#define to_msensor(_dev) \
	container_of(_dev, struct msensor_device, dev)

/*
 * Some sensors (i.e. UART) send floating point numbers so we need to convert
 * them to integers to be able to handle them in the kernel.
 */

/**
 * msensor_ftoi - convert 32-bit IEEE 754 float to fixed point integer
 * @f: The floating point number.
 * @dp: The number of decimal places in the fixed-point integer.
 */
int msensor_ftoi(uint f, unsigned dp)
{
	int s = (f & 0x80000000) ? -1 : 1;
	unsigned char e = (f & 0x7F800000) >> 23;
	unsigned long i = f & 0x007FFFFFL;
	unsigned long m;

	/* handle special cases for zero, +/- infinity and NaN */
	if (!e)
		return 0;
	if (e == 255)
		return s == 1 ? INT_MAX : INT_MIN;

	i += 1 << 23;
	while (dp--)
		i *= 10;
	if (e < 150) {
		m = i % (1L << (150 - e));
		i += m >> 1;
		i >>= 150 - e;
	}
	else
		i <<= e - 150;

	return s * i;
}

/**
 * msensor_itof - convert fixed point integer to 32-bit IEEE 754 float
 * @i: The fixed-point integer.
 * @dp: The number of decimal places in the fixed-point integer.
 */
uint msensor_itof(int i, unsigned dp)
{
	int s = i < 0 ? -1 : 1;
	unsigned char e = 127;
	unsigned long f = i * s;

	/* special case for zero */
	if (i == 0)
		return 0;

	f <<= 23;
	while (dp-- > 0)
		f /= 10;

	while (f >= (1 << 24)) {
		f >>= 1;
		e++;
	}
	while (f < (1 << 23)) {
		f <<= 1;
		e--;
	}
	f -= 1 << 23;
	if (s == -1)
		f |= 0x80000000;

	return f | e << 23;
}

static ssize_t msensor_show_type_id(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct msensor_device *ms = to_msensor(dev);

	return sprintf(buf, "%d\n", ms->type_id);
}

static ssize_t msensor_show_mode(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int i;
	unsigned count = 0;
	int mode = ms->get_mode(ms->context);

	for (i = 0; i < ms->num_modes; i++) {
		if (i == mode)
			count += sprintf(buf + count, "[");
		count += sprintf(buf + count, "%s", ms->mode_info[i].name);
		if (i == mode)
			count += sprintf(buf + count, "]");
		count += sprintf(buf + count, "%c", ' ');
	}
	if (count == 0)
		return -ENXIO;
	buf[count - 1] = '\n';

	return count;
}

static ssize_t msensor_store_mode(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct msensor_device *ms = to_msensor(dev);
	int i, err;

	for (i = 0; i < ms->num_modes; i++) {
		if (sysfs_streq(buf, ms->mode_info[i].name)) {
			err = ms->set_mode(ms->context, i);
			if (err)
				return err;
			return count;
		}
	}
	return -EINVAL;
}

/* common definition for the min/max properties (float data)*/
#define MSENSOR_SHOW_F(_name)							\
static ssize_t msensor_show_##_name(struct device *dev,				\
				    struct device_attribute *attr,		\
				    char *buf)					\
{										\
	struct msensor_device *ms = to_msensor(dev);				\
	u8 mode = ms->get_mode(ms->context);					\
	int value = ms->mode_info[mode]._name;					\
	int dp = ms->mode_info[mode].decimals;					\
										\
	return sprintf(buf, "%d\n", msensor_ftoi(value, dp));		\
}

MSENSOR_SHOW_F(raw_min)
MSENSOR_SHOW_F(raw_max)
MSENSOR_SHOW_F(pct_min)
MSENSOR_SHOW_F(pct_max)
MSENSOR_SHOW_F(si_min)
MSENSOR_SHOW_F(si_max)

static ssize_t msensor_show_si_units(struct device *dev,
                                     struct device_attribute *attr,
                                     char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);

	return sprintf(buf, "%s\n",ms->mode_info[mode].units);
}

static ssize_t msensor_show_dp(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);

	return sprintf(buf, "%d\n", ms->mode_info[mode].decimals);
}

static ssize_t msensor_show_num_values(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);

	return sprintf(buf, "%d\n", ms->mode_info[mode].data_sets);
}

int msensor_raw_s8_value(struct msensor_device *ms, int index)
{
	int mode = ms->get_mode(ms->context);

	return *(s8 *)(ms->mode_info[mode].raw_data + index);
}

int msensor_raw_s16_value(struct msensor_device *ms, int index)
{
	int mode = ms->get_mode(ms->context);

	return *(s16 *)(ms->mode_info[mode].raw_data + index * 2);
}

int msensor_raw_s32_value(struct msensor_device *ms, int index)
{
	int mode = ms->get_mode(ms->context);

	return *(s32 *)(ms->mode_info[mode].raw_data + index * 4);
}

int msensor_raw_float_value(struct msensor_device *ms, int index)
{
	int mode = ms->get_mode(ms->context);

	return msensor_ftoi(
		*(u32 *)(ms->mode_info[mode].raw_data + index * 4),
		ms->mode_info[mode].decimals);
}

static ssize_t msensor_show_value(struct device *dev,
                                  struct device_attribute *attr,
                                  char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);
	int count = -ENXIO;
	int index;

	if (strlen(attr->attr.name) < 6)
		return count;
	if (sscanf(attr->attr.name + 5, "%d", &index) != 1)
		return count;
	if (index < 0 || index >= ms->mode_info[mode].data_sets)
		return count;

	switch (ms->mode_info[mode].format) {
	case MSENSOR_DATA_8:
		count = sprintf(buf, "%d\n",
			msensor_raw_s8_value(ms, index));
		break;
	case MSENSOR_DATA_16:
		count = sprintf(buf, "%d\n",
			msensor_raw_s16_value(ms, index));
		break;
	case MSENSOR_DATA_32:
		count = sprintf(buf, "%d\n",
			msensor_raw_s32_value(ms, index));
		break;
	case MSENSOR_DATA_FLOAT:
		count = sprintf(buf, "%d\n",
			msensor_raw_float_value(ms, index));
		break;
	}

	return count;
}

static ssize_t msensor_show_bin_data_format(struct device *dev,
                                            struct device_attribute *attr,
                                            char *buf)
{
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);
	int count = -ENXIO;

	switch (ms->mode_info[mode].format) {
	case MSENSOR_DATA_8:
		count = sprintf(buf, "%s\n", "s8");
		break;
	case MSENSOR_DATA_16:
		count = sprintf(buf, "%s\n", "s16");
		break;
	case MSENSOR_DATA_32:
		count = sprintf(buf, "%s\n", "s32");
		break;
	case MSENSOR_DATA_FLOAT:
		count = sprintf(buf, "%s\n", "float");
		break;
	}

	return count;
}

static ssize_t msensor_read_bin_data(struct file *file, struct kobject *kobj,
                                     struct bin_attribute *attr,
                                     char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct msensor_device *ms = to_msensor(dev);
	int mode = ms->get_mode(ms->context);
	size_t size = attr->size;

	if (off >= size || !count)
		return 0;
	size -= off;
	if (count < size)
		size = count;
	memcpy(buf + off, ms->mode_info[mode].raw_data, size);

	return size;
}

static ssize_t msensor_write_bin_data(struct file *file ,struct kobject *kobj,
                                      struct bin_attribute *attr,
                                      char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct msensor_device *ms = to_msensor(dev);

	return ms->write_data(ms->context, buf, off, count);
}

static struct device_attribute msensor_device_attrs[] = {
	__ATTR(type_id, S_IRUGO, msensor_show_type_id, NULL),
	__ATTR(mode, S_IRUGO | S_IWUGO, msensor_show_mode, msensor_store_mode),
	__ATTR(raw_min, S_IRUGO, msensor_show_raw_min, NULL),
	__ATTR(raw_max, S_IRUGO, msensor_show_raw_max, NULL),
	__ATTR(pct_min, S_IRUGO, msensor_show_pct_min, NULL),
	__ATTR(pct_max, S_IRUGO, msensor_show_pct_max, NULL),
	__ATTR(si_min, S_IRUGO, msensor_show_si_min, NULL),
	__ATTR(si_max, S_IRUGO, msensor_show_si_max, NULL),
	__ATTR(si_units, S_IRUGO, msensor_show_si_units, NULL),
	__ATTR(dp, S_IRUGO, msensor_show_dp, NULL),
	__ATTR(num_values, S_IRUGO, msensor_show_num_values, NULL),
	__ATTR(bin_data_format, S_IRUGO, msensor_show_bin_data_format, NULL),
	/*
	 * Technically, it is possible to have 32 8-bit values from UART sensors and
	 * 255 8-bit values from I2C sensors, but known sensors so far are 8 or less,
	 * so we only expose 8 values to prevent sysfs overcrowding.
	 */
	__ATTR(value0, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value1, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value2, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value3, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value4, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value5, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value6, S_IRUGO , msensor_show_value, NULL),
	__ATTR(value7, S_IRUGO , msensor_show_value, NULL),
	__ATTR_NULL
};

static struct bin_attribute msensor_device_bin_attrs[] = {
	{
		.attr	= {
			.name	= "bin_data",
			.mode	= S_IRUGO,
		},
		.size	= MSENSOR_RAW_DATA_SIZE,
		.read	= msensor_read_bin_data,
		.write	= msensor_write_bin_data,
	},
	__ATTR_NULL
};

static void msensor_release(struct device *dev)
{
}

int register_msensor(struct msensor_device *ms, struct device *parent)
{
	if (!ms || !parent)
		return -EINVAL;

	ms->dev.release = msensor_release;
	ms->dev.parent = parent;
	ms->dev.class = &msensor_class;
	dev_set_name(&ms->dev, "%s", dev_name(parent));

	return device_register(&ms->dev);
}
EXPORT_SYMBOL_GPL(register_msensor);

void unregister_msensor(struct msensor_device *ms)
{
	device_unregister(&ms->dev);
}
EXPORT_SYMBOL_GPL(unregister_msensor);

static char *msensor_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "msensor/%s", dev_name(dev->parent));
}

struct class msensor_class = {
	.name		= "msensor",
	.owner		= THIS_MODULE,
	.dev_attrs	= msensor_device_attrs,
	.dev_bin_attrs	= msensor_device_bin_attrs,
	.devnode	= msensor_devnode,
};
EXPORT_SYMBOL_GPL(msensor_class);

static int __init msensor_class_init(void)
{
	int err;

	err = class_register(&msensor_class);
	if (err) {
		pr_err("unable to register msensor device class\n");
		return err;
	}

	return 0;
}
module_init(msensor_class_init);

static void __exit msensor_class_exit(void)
{
	class_unregister(&msensor_class);
}
module_exit(msensor_class_exit);

MODULE_DESCRIPTION("Mindstorms sensor device class for LEGO Mindstorms EV3");
MODULE_AUTHOR("David Lechner <david@lechnology.com>");
MODULE_LICENSE("GPL");