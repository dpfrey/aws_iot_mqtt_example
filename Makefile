
PAHO_DIR=../paho.mqtt.c

aws_iot_mqtt_example: main.c
	gcc \
		-I $(PAHO_DIR)/src \
		-L $(PAHO_DIR)/build/output \
		-lpaho-mqtt3as \
		-o aws_iot_mqtt_example \
		main.c
