#define DT_DRV_COMPAT zephyr_accel_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>

#include "accel-sensor.h"
#include <math.h>

LOG_MODULE_REGISTER(accel_sensor, LOG_LEVEL_INF);

#if !defined(M_PIf)
#define M_PIf 3.1415927f
#endif


// Функція для обчислення довжини вектора
float vector_length(_Vector3 v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

// Функція для обчислення косинуса кута між двома векторами
float angle_between_vectors(_Vector3 v1, _Vector3 v2) {
    float dot_product = v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    float magnitude_product = vector_length(v1) * vector_length(v2);
    if (magnitude_product == 0) return 0.0f; // уникнення ділення на нуль
    return acosf(dot_product / magnitude_product); // кут у радіанах
}

// Функція для обчислення кутів нахилу по осях
void calculate_tilt_angles(_Vector3 ref, _Vector3 current, float *theta_x, float *theta_y, float *theta_z) {
    *theta_x = atanf(current.y / current.z) - atanf(ref.y / ref.z);
    *theta_y = atanf(current.x / current.z) - atanf(ref.x / ref.z);
    *theta_z = atanf(current.x / current.y) - atanf(ref.x / ref.y);
}

int get_mma8652_val(const struct device *dev, struct sensor_value *val)
{
	if (sensor_sample_fetch(dev)) {
		printk("Failed to fetch sample for device %s\n",
		       dev->name);
		return -1;
	}

	if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_XYZ, &val[0])) {
		return -1;
	}

	return 0;
}


static void adc_vbus_work_handler(struct k_work *work)
{
    struct k_work_delayable *delayable = k_work_delayable_from_work(work);
    struct accel_sensor_data *data = CONTAINER_OF(delayable, struct accel_sensor_data, dwork);
	const struct device *dev = data->accel_dev;

	struct sensor_value val[3];

    get_mma8652_val(dev, val);
    float ax = sensor_value_to_double(&val[0]);
    float ay = sensor_value_to_double(&val[1]);
    float az = sensor_value_to_double(&val[2]);
    _Vector3 current_acc = {ax, ay, az};
    data->last_acc = current_acc;

    // Обчислення загального кута нахилу
    float theta = angle_between_vectors(data->ref_acc, current_acc);
    // printk("Загальний кут нахилу: %.3f градусів\n", theta * (180.0f / M_PI));

	LOG_DBG(
		"Current values: X:%.4f Y:%.4f Z:%.4f"
		" Tilt angle: %.4f"
		, (double)ax, (double)ay, (double)az
		, (double)theta
	);

	// TODO: Add callback
	#if 0
    	balance_overwrite_value((100.0f * theta * (180.0f / M_PIf)));
	#endif
    // force_send_state();

    // LOG_ERR(
    //     "Move sensor value X:%10.3f Y:%10.3f X:%10.3f",
    //     sensor_value_to_double(&val[0]),
    //     sensor_value_to_double(&val[1]),
    //     sensor_value_to_double(&val[2])
    //     // val[0].val1, val[1].val1, val[2].val1
    // );
    // adc_vbus_process();
    k_work_schedule(&data->dwork, K_MSEC(data->sampling_period_ms));
}


// API
static int _save_current_positoin_as_reference(const struct device *dev)
{
	struct accel_sensor_data *data = dev->data;
	data->ref_acc = data->last_acc;
	return 0;
}


static int init(const struct device *dev)
{
	const struct accel_sensor_config *cfg = dev->config;
	struct accel_sensor_data *data = dev->data;

	LOG_DBG("Initializing Accelerometer Sensor (%s)", dev->name);

	const struct device *adev = cfg->accel_dev;
	if (!device_is_ready(adev)) {
		LOG_ERR("Accelerometer device %s not ready", adev->name);
		return -ENODEV;
	}
	
	LOG_DBG("Accelerometer device: %s is ready", adev->name);
	// struct accel_sensor_data *data = dev->data;
	int rc;

    #if 0
	rc = _set_sample_period(dev, data->sample_period_ms);
	if (rc != 0) {
		LOG_WRN("Failed to set sample period. Using period stored of device");
		/* Try to read sample time from sensor to reflect actual sample period */
		rc = _get_sample_time(dev);
	}

	LOG_DBG("Sample period: %d", data->sample_period_ms);
	if (rc != 0) {
		return rc;
	}
    #endif
    
	LOG_DBG("Starting periodic measurements (%d ms)", data->sampling_period_ms);

	// TODO: Напевно треба перенести в макрос визначення змінної
	k_work_init_delayable(&data->dwork, adc_vbus_work_handler);
	k_work_schedule(&data->dwork, K_MSEC(data->sampling_period_ms));

	return rc;
}

#if 0
static int _attr_get(const struct device *dev, enum sensor_channel chan,
	enum sensor_attribute attr, struct sensor_value *val)
{
	// struct scd30_data *data = dev->data;
	return -ENOTSUP;
}

static int _attr_set(const struct device *dev, enum sensor_channel chan,
	enum sensor_attribute attr, const struct sensor_value *val)
{
	return -ENOTSUP;
}
#endif

static const struct accel_sensor_driver_api driver_api = {
	.set_current_position_as_reference = _save_current_positoin_as_reference,
	// .sample_fetch = scd30_sample_fetch,
	// .channel_get = scd30_channel_get,
	// .attr_get = _attr_get,
	// .attr_set = _attr_set,
};

#define ACCEL_SENSOR_DEFINE(inst)									    \
	static struct accel_sensor_data data_##inst = {						\
		.sampling_period_ms = DT_INST_PROP(inst, sampling_period_ms),		\
		.accel_dev = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))), 			\
	};											                        \
	static const struct accel_sensor_config config_##inst = {			\
		.accel_dev = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(inst))), 			\
	};											                        \
												                        \
	DEVICE_DT_INST_DEFINE(inst, init, NULL, &data_##inst, &config_##inst,	\
			      POST_KERNEL, CONFIG_ACCEL_SENSOR_INIT_PRIORITY, &driver_api);

// CONFIG_KERNEL_INIT_PRIORITY_DEVICE

DT_INST_FOREACH_STATUS_OKAY(ACCEL_SENSOR_DEFINE);
