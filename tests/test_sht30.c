/* Run the production driver against deterministic I2C and FreeRTOS mocks.
 * The harness uses no C runtime functions; build it with a host C compiler. */
#include "sht30.h"
#include "freertos/task.h"

#ifdef _WIN32
int _fltused = 0;
#endif

struct mock_i2c_bus { int unused; };
struct mock_i2c_device { int unused; };
static struct mock_i2c_bus bus_storage;
static struct mock_i2c_device device_storage;
#define BUS (&bus_storage)
#define DEVICE (&device_storage)

enum operation { ADD, DELAY, TRANSMIT, RECEIVE, REMOVE };
struct event { enum operation operation; unsigned int value; };
static struct event events[32];
static unsigned int event_count;
static int mock_fault;
static esp_err_t add_result, transmit_result, receive_result, remove_result;
static uint8_t response[6];

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
#define MOCK_CHECK(condition) do { if (!(condition) && !mock_fault) mock_fault = __LINE__; } while (0)
#define CHECK_EVENT(index, kind, expected) do { \
    CHECK(event_count > (index)); \
    CHECK(events[index].operation == (kind)); \
    CHECK(events[index].value == (expected)); \
} while (0)
#define CHECK_MOCK() CHECK(mock_fault == 0)
#define WAIT_TICKS (pdMS_TO_TICKS(20) + 2)

static void record(enum operation operation, unsigned int value)
{
    MOCK_CHECK(event_count < 32);
    if (event_count < 32) {
        events[event_count].operation = operation;
        events[event_count].value = value;
        ++event_count;
    }
}

static void set_frame(uint8_t t_msb, uint8_t t_lsb, uint8_t t_crc,
                      uint8_t h_msb, uint8_t h_lsb, uint8_t h_crc)
{
    response[0] = t_msb; response[1] = t_lsb; response[2] = t_crc;
    response[3] = h_msb; response[4] = h_lsb; response[5] = h_crc;
}

static void reset_mock(void)
{
    event_count = 0;
    mock_fault = 0;
    add_result = transmit_result = receive_result = remove_result = ESP_OK;
    /* Sensirion's published CRC example: BE EF -> 92. */
    set_frame(0xBE, 0xEF, 0x92, 0xBE, 0xEF, 0x92);
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus,
                                  const i2c_device_config_t *config,
                                  i2c_master_dev_handle_t *device)
{
    MOCK_CHECK(bus == BUS && config != NULL && device != NULL);
    if (!config || !device) return ESP_ERR_INVALID_ARG;
    MOCK_CHECK(config->dev_addr_length == I2C_ADDR_BIT_LEN_7);
    MOCK_CHECK(config->scl_speed_hz == 100000);
    record(ADD, config->device_address);
    if (add_result == ESP_OK) *device = DEVICE;
    return add_result;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t device)
{
    MOCK_CHECK(device == DEVICE);
    record(REMOVE, 0);
    return remove_result;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device,
                           const uint8_t *data, size_t length, int timeout_ms)
{
    MOCK_CHECK(device == DEVICE && data != NULL);
    MOCK_CHECK(length == 2 && timeout_ms == 100);
    record(TRANSMIT, data && length == 2 ? ((unsigned int)data[0] << 8) | data[1] : 0);
    return transmit_result;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t device,
                          uint8_t *data, size_t length, int timeout_ms)
{
    MOCK_CHECK(device == DEVICE && data != NULL);
    MOCK_CHECK(length == 6 && timeout_ms == 100);
    record(RECEIVE, (unsigned int)length);
    /* Even failed reads may alter their receive buffer, never caller outputs. */
    if (data) for (size_t i = 0; i < length && i < 6; ++i) data[i] = response[i];
    return receive_result;
}

void vTaskDelay(TickType_t ticks)
{
    MOCK_CHECK(ticks == WAIT_TICKS);
    /* A delay may start immediately before the next scheduler tick. */
    MOCK_CHECK(ticks > 0 && (ticks - 1) * 1000U >= 20U * CONFIG_FREERTOS_HZ);
    record(DELAY, ticks);
}

static int init_invalid_arguments(void)
{
    sht30_t sensor = {0};
    CHECK(sht30_init(NULL, BUS, 0x44) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_init(&sensor, NULL, 0x44) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_init(&sensor, BUS, 0x00) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_init(&sensor, BUS, 0x40) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_init(&sensor, BUS, 0x88) == ESP_ERR_INVALID_ARG);
    CHECK(event_count == 0 && sensor.i2c_dev == NULL && !sensor.initialized);
    return 0;
}

static int init_default_address_and_sequence(void)
{
    sht30_t sensor = {0};
    CHECK(sht30_init(&sensor, BUS, SHT30_I2C_ADDRESS_DEFAULT) == ESP_OK);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK(event_count == 4);
    CHECK_EVENT(0, ADD, 0x44);
    CHECK_EVENT(1, DELAY, WAIT_TICKS);
    CHECK_EVENT(2, TRANSMIT, 0x30A2);
    CHECK_EVENT(3, DELAY, WAIT_TICKS);
    CHECK_MOCK();
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 4 && sensor.i2c_dev == DEVICE && sensor.initialized);
    return 0;
}

static int init_alternate_address(void)
{
    sht30_t sensor = {0};
    CHECK(sht30_init(&sensor, BUS, SHT30_I2C_ADDRESS_ALTERNATE) == ESP_OK);
    CHECK_EVENT(0, ADD, 0x45);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK_MOCK();
    return 0;
}

static int init_add_failure_and_retry(void)
{
    sht30_t sensor = {0};
    add_result = ESP_ERR_NO_MEM;
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_ERR_NO_MEM);
    CHECK(event_count == 1 && sensor.i2c_dev == NULL && !sensor.initialized);
    CHECK_MOCK();
    reset_mock();
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_OK);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK_MOCK();
    return 0;
}

static int init_reset_failure_and_retry(void)
{
    sht30_t sensor = {0};
    transmit_result = ESP_ERR_TIMEOUT;
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_ERR_TIMEOUT);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized);
    CHECK(event_count == 4);
    CHECK_EVENT(0, ADD, 0x44);
    CHECK_EVENT(1, DELAY, WAIT_TICKS);
    CHECK_EVENT(2, TRANSMIT, 0x30A2);
    CHECK_EVENT(3, REMOVE, 0);
    CHECK_MOCK();
    reset_mock();
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_OK);
    CHECK(sensor.initialized);
    CHECK_MOCK();
    return 0;
}

static int init_cleanup_failure_preserves_handle(void)
{
    sht30_t sensor = {0};
    float temperature = 123.0f, humidity = 456.0f;
    transmit_result = ESP_ERR_TIMEOUT;
    remove_result = ESP_FAIL;
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_ERR_TIMEOUT);
    CHECK(sensor.i2c_dev == DEVICE && !sensor.initialized);
    CHECK(event_count == 4);
    CHECK_EVENT(3, REMOVE, 0);
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_ERR_INVALID_STATE);
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 4 && temperature == 123.0f && humidity == 456.0f);
    CHECK_MOCK();
    reset_mock();
    CHECK(sht30_deinit(&sensor) == ESP_OK);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized);
    CHECK(event_count == 1);
    CHECK_EVENT(0, REMOVE, 0);
    CHECK(sht30_init(&sensor, BUS, 0x44) == ESP_OK);
    CHECK(sensor.initialized);
    CHECK_MOCK();
    return 0;
}

static int read_invalid_arguments_and_state(void)
{
    sht30_t sensor = {0};
    float temperature = 123.0f, humidity = 456.0f;
    CHECK(sht30_read(NULL, &temperature, &humidity) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_read(&sensor, NULL, &humidity) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_read(&sensor, &temperature, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_STATE);
    sensor.i2c_dev = DEVICE;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_STATE);
    sensor.i2c_dev = NULL;
    sensor.initialized = true;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 0 && temperature == 123.0f && humidity == 456.0f);
    return 0;
}

static int read_known_crc_vector_and_sequence(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 0.0f, humidity = 0.0f;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_OK);
    CHECK(temperature > 85.5229f && temperature < 85.5231f);
    CHECK(humidity > 74.5844f && humidity < 74.5848f);
    CHECK(event_count == 3);
    CHECK_EVENT(0, TRANSMIT, 0x2400);
    CHECK_EVENT(1, DELAY, WAIT_TICKS);
    CHECK_EVENT(2, RECEIVE, 6);
    CHECK_MOCK();
    return 0;
}

static int read_full_scale_conversion(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 0.0f, humidity = 0.0f;
    set_frame(0, 0, 0x81, 0xFF, 0xFF, 0xAC);
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_OK);
    CHECK(temperature == -45.0f && humidity == 100.0f);
    set_frame(0xFF, 0xFF, 0xAC, 0, 0, 0x81);
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_OK);
    CHECK(temperature == 130.0f && humidity == 0.0f);
    CHECK_MOCK();
    return 0;
}

static int read_temperature_crc_failure(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 123.0f, humidity = 456.0f;
    response[2] ^= 1;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_CRC);
    CHECK(temperature == 123.0f && humidity == 456.0f);
    CHECK(event_count == 3);
    CHECK_MOCK();
    return 0;
}

static int read_humidity_crc_failure(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 123.0f, humidity = 456.0f;
    response[5] ^= 1;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_CRC);
    CHECK(temperature == 123.0f && humidity == 456.0f);
    CHECK(event_count == 3);
    CHECK_MOCK();
    return 0;
}

static int read_transmit_failure_and_retry(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 123.0f, humidity = 456.0f;
    transmit_result = ESP_ERR_TIMEOUT;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_TIMEOUT);
    CHECK(temperature == 123.0f && humidity == 456.0f);
    CHECK(event_count == 1 && sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK_EVENT(0, TRANSMIT, 0x2400);
    CHECK_MOCK();
    reset_mock();
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_OK);
    CHECK_MOCK();
    return 0;
}

static int read_receive_failure_and_retry(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 123.0f, humidity = 456.0f;
    receive_result = ESP_FAIL;
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_FAIL);
    CHECK(temperature == 123.0f && humidity == 456.0f);
    CHECK(event_count == 3 && sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK_EVENT(2, RECEIVE, 6);
    CHECK_MOCK();
    reset_mock();
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_OK);
    CHECK_MOCK();
    return 0;
}

static int deinit_invalid_arguments_and_state(void)
{
    sht30_t sensor = {0};
    CHECK(sht30_deinit(NULL) == ESP_ERR_INVALID_ARG);
    CHECK(sht30_deinit(&sensor) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 0);
    return 0;
}

static int deinit_failure_and_retry(void)
{
    sht30_t sensor = {DEVICE, true};
    float temperature = 123.0f, humidity = 456.0f;
    remove_result = ESP_ERR_TIMEOUT;
    CHECK(sht30_deinit(&sensor) == ESP_ERR_TIMEOUT);
    CHECK(sensor.i2c_dev == DEVICE && sensor.initialized);
    CHECK(event_count == 1);
    CHECK_EVENT(0, REMOVE, 0);
    remove_result = ESP_OK;
    CHECK(sht30_deinit(&sensor) == ESP_OK);
    CHECK(sensor.i2c_dev == NULL && !sensor.initialized);
    CHECK(event_count == 2);
    CHECK_EVENT(1, REMOVE, 0);
    CHECK(sht30_deinit(&sensor) == ESP_ERR_INVALID_STATE);
    CHECK(sht30_read(&sensor, &temperature, &humidity) == ESP_ERR_INVALID_STATE);
    CHECK(event_count == 2 && temperature == 123.0f && humidity == 456.0f);
    CHECK_MOCK();
    return 0;
}

struct test_case { const char *name; int (*run)(void); };
#define TEST(name) {#name, name}
static const struct test_case cases[] = {
    TEST(init_invalid_arguments),
    TEST(init_default_address_and_sequence),
    TEST(init_alternate_address),
    TEST(init_add_failure_and_retry),
    TEST(init_reset_failure_and_retry),
    TEST(init_cleanup_failure_preserves_handle),
    TEST(read_invalid_arguments_and_state),
    TEST(read_known_crc_vector_and_sequence),
    TEST(read_full_scale_conversion),
    TEST(read_temperature_crc_failure),
    TEST(read_humidity_crc_failure),
    TEST(read_transmit_failure_and_retry),
    TEST(read_receive_failure_and_retry),
    TEST(deinit_invalid_arguments_and_state),
    TEST(deinit_failure_and_retry),
};

int sht30_test_count(void) { return (int)(sizeof(cases) / sizeof(cases[0])); }
const char *sht30_test_name(int index) { return cases[index].name; }
int sht30_test_run(int index)
{
    reset_mock();
    int line = cases[index].run();
    return line ? line : mock_fault;
}
