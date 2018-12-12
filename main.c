#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include "MQTTAsync.h"

static const char *mqtt_host = "acsf8ikktv204-ats.iot.us-west-2.amazonaws.com";
static const uint16_t mqtt_port = 8883;
static const char *ca_path = "../certs/ca";
/* static const char *ca_file = "../certs/ca/AmazonRootCA1.pem"; */
static const char *cert_path = "../certs/04ada7f21c-certificate.pem.crt";
static const char *private_key_path = "../certs/04ada7f21c-private.pem.key";
static const char *public_key_path = "../certs/04ada7f21c-public.pem.key";

static const char *role_publisher = "publisher";
static const char *role_subscriber = "subscriber";

enum Role {
    ROLE_PUBLISHER,
    ROLE_SUBSCRIBER,
};

struct PublisherContext {
    MQTTAsync client;
};

static bool determine_role(char **args, size_t num_args, enum Role *role)
{
    if (num_args != 2)
        return false;

    if (strcmp(args[1], role_publisher) == 0) {
        *role = ROLE_PUBLISHER;
        return true;
    }

    if (strcmp(args[1], role_subscriber) == 0) {
        *role = ROLE_SUBSCRIBER;
        return true;
    }

    return false;
}

static size_t hex_dump(const uint8_t *in, size_t in_size, char *buffer, size_t buffer_size)
{
    // "AAAA | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F"
    const size_t bytes_per_line = 55;
    const size_t data_lines = (in_size / 16) + ((in_size % 16) ? 1 : 0);
    const char *header1 = " +   | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n";
    const char *header2 = "-----+------------------------------------------------\n";
    const size_t bytes_required = (bytes_per_line * (data_lines + 2)) + 1;

    if (buffer_size < bytes_required)
        return bytes_required;
    size_t offset = 0;
    memcpy(&buffer[offset], header1, bytes_per_line);
    offset += bytes_per_line;
    memcpy(&buffer[offset], header2, bytes_per_line);
    offset += bytes_per_line;

    for (size_t i = 0; i < data_lines; i++) {
        size_t b = i * 16;
        offset += sprintf(&buffer[offset], "%04X |", b);
        for (size_t j = b; j < (b + 0x10), j < in_size; j++) {
            offset += sprintf(&buffer[offset], " %02X", in[j]);
        }
        buffer[offset] = '\n';
        offset++;
    }

    return bytes_required;
}

static void connection_lost_handler(void *context, char *cause)
{
    printf("Connection lost.  cause: %s\n", cause);
}

static int message_arrived_handler(
    void *context, char *topicName, int topicLen, MQTTAsync_message *message)
{
    bool message_received = true;
    size_t payload_disp_buffer_size = hex_dump(message->payload, message->payloadlen, NULL, 0);
    char *payload_disp_buffer = calloc(payload_disp_buffer_size, 1);
    assert(payload_disp_buffer);
    assert(
        hex_dump(
            message->payload,
            message->payloadlen,
            payload_disp_buffer,
            payload_disp_buffer_size) == payload_disp_buffer_size);

    printf("Message arrived on topic=%s, payload:\n", topicName);
    fputs(payload_disp_buffer, stdout);

    return message_received;
}

static void delivery_complete_handler(void *context, MQTTAsync_token token)
{
    printf("Delivery complete\n");
    // TODO: Can the token be inspected in any way?
}

static void connect_success_handler(void *context, MQTTAsync_successData *response)
{
    printf("Connection success!\n");
}

static void connect_failure_handler(void *context, MQTTAsync_failureData *response)
{
    printf("Connection failed with code=%d, message=%s\n", response->code, response->message);
}

static int ssl_error_callback(const char *str, size_t len, void *u)
{
    printf("In ssl_error_callback with str=%s\n", str);
    return 0;
}

static void become_publisher(void)
{
    char address[512] = {0};
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    struct PublisherContext *pc = calloc(sizeof(*pc), 1);
    assert(pc);

    const int address_len = snprintf(address, sizeof(address), "ssl://%s:%d", mqtt_host, mqtt_port);
    assert(address_len < sizeof(address));

    assert(
        MQTTAsync_create(
            &pc->client,
            address,
            "example-publisher",
            MQTTCLIENT_PERSISTENCE_NONE,
            NULL) == MQTTASYNC_SUCCESS);

    assert(
        MQTTAsync_setCallbacks(
            pc->client,
            pc,
            connection_lost_handler,
            message_arrived_handler,
            delivery_complete_handler) == MQTTASYNC_SUCCESS);

    MQTTAsync_SSLOptions ssl_opts = MQTTAsync_SSLOptions_initializer;
    ssl_opts.trustStore = cert_path;
    /* ssl_opts.keyStore = public_key_path; */
    ssl_opts.privateKey = private_key_path;
    /* ssl_opts.enableServerCertAuth = true; */
    ssl_opts.CApath = ca_path;
    /* ssl_opts.CAfile = ca_file; */
    /* ssl_opts.sslVersion = MQTT_SSL_VERSION_TLS_1_2; */
    ssl_opts.ssl_error_cb = ssl_error_callback;

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;
    conn_opts.onSuccess = connect_success_handler;
    conn_opts.onFailure = connect_failure_handler;
    conn_opts.context = pc;
    conn_opts.ssl = &ssl_opts;
    int conn_res = MQTTAsync_connect(pc->client, &conn_opts);
    if (conn_res != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "Attempt to initiate connection failed with error code: %d\n", conn_res);
        exit(1);
    }

    bool interrupted = (sleep(20) != 0);
    if (interrupted)
        printf("Who dares to interrupt my slumber!\n");

    MQTTAsync_destroy(&pc->client);
}

static void become_subscriber(void)
{
}

int main(int argc, char **argv)
{
    enum Role role;
    if (!determine_role(argv, argc, &role)) {
        fprintf(stderr, "Invalid arguments\n");
        exit(1);
    }

    MQTTAsync_init_options init_opts = MQTTAsync_init_options_initializer;
    init_opts.do_openssl_init = 1;
    MQTTAsync_global_init(&init_opts);

    switch (role) {
    case ROLE_PUBLISHER:
        become_publisher();
        break;

    case ROLE_SUBSCRIBER:
        become_subscriber();
        break;

    default:
        fprintf(stderr, "Invalid role\n");
        exit(1);
        break;
    }

    printf("All finished\n");
    exit(0);
}
