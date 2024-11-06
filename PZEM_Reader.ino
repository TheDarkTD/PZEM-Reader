/********************************************************************************
 * Nome: Felipe Rodrigues Moreira Dos Santos
 * E-mail: axfeliperodrigues@gmail.com
 * Data: 19/10/2024
 * Descrição: Codigo para captura e envio de dados do pzem para um broker
 * Versão: 1.2
 * GitHub: https://github.com/TheDarkTD/PZEM-Reader
 ********************************************************************************/



#include <PZEM004Tv30.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define DEFAULT_TEMPO_ESPERA 20000  // 20 segundos padrão em milissegundos
#define MIN_TEMPO_ESPERA 5000         // Valor mínimo permitido para o tempo de espera
#define PZEM_RX_PIN 16
#define PZEM_TX_PIN 17
#define PZEM_SERIAL Serial2
#define LED 2

PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);

float voltage = 0;
float current = 0;
float power = 0;
float energy = 0;
float frequency = 0;
float pf = 0;

unsigned long previous = 0;
unsigned long tempo_espera = DEFAULT_TEMPO_ESPERA; // Tempo de espera inicial

// Credenciais MQTT da Adafruit IO
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883                   
#define AIO_USERNAME    "TheDarkTD"
#define AIO_KEY         "aio_knzR09P6BLThZ6wVKvYrsZ7GvRXL"

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Tópicos MQTT para envio de dados
Adafruit_MQTT_Publish pzem_voltage = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/voltage");
Adafruit_MQTT_Publish pzem_current = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/current");
Adafruit_MQTT_Publish pzem_power = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/power");
Adafruit_MQTT_Publish pzem_energy = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/energy");
Adafruit_MQTT_Publish pzem_frequency = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/frequency");
Adafruit_MQTT_Publish pzem_pf = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/pf");
Adafruit_MQTT_Publish pzem_error = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/errors");

// URL para pegar o tempo de espera
const char* espera_url = "https://io.adafruit.com/api/v2/TheDarkTD/feeds/espera"; 
unsigned long lastUpdate = 0; // Para armazenar o último tempo de atualização

void setup() {
    Serial.begin(115200);
    
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH);

    // Gerenciador Wi-Fi
    WiFiManager wifiManager;
    wifiManager.autoConnect("PZEM_MQTT_AP");
    
    Serial.println("Conectado à rede WiFi");

    // Conexão inicial ao MQTT
    connectToMQTT();
}

void loop() {
    // Reconectar ao Wi-Fi se necessário
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconectando WiFi...");
        WiFi.reconnect();
        delay(2000);
    }

    // Reconectar ao MQTT se necessário
    if (!mqtt.connected()) {
        connectToMQTT();
    }

    // Escutar os tópicos MQTT
    mqtt.processPackets(500);

    // Atualiza o tempo de espera a cada 250 ms
    unsigned long now = millis();
    if (now - lastUpdate >= tempo_espera) { // Verifica se 250 ms se passaram
        lastUpdate = now; // Atualiza o último tempo de atualização
        updateWaitTime(); // Atualiza o tempo de espera
    }

    // Lê os dados do sensor e publica no MQTT
    if ((now - previous >= tempo_espera) || (previous == 0)) {
        PZEM();
        sendDataToMQTT();
        previous = now;
    }

    digitalWrite(LED, !digitalRead(LED));
    delay(500);
}

// Função para buscar o tempo de espera via HTTP
void updateWaitTime() {
    HTTPClient http;
    http.begin(espera_url);
    int httpResponseCode = http.GET(); // Faz a requisição GET

    if (httpResponseCode > 0) {
        String payload = http.getString(); // Obtém o conteúdo da resposta
       // Serial.print("Payload recebido: ");
        //Serial.println(payload); // Mensagem de depuração

        // Processa a resposta JSON
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            // Acessa o valor de last_value do JSON
            unsigned long newTempoEspera = doc["last_value"].as<unsigned long>(); // Converte para inteiro

            Serial.print("Novo tempo de espera recebido: "); // Mensagem de depuração
            Serial.println(newTempoEspera);

            // Verifica se o novo tempo de espera é diferente do atual
            if (newTempoEspera != tempo_espera) {
                // Verifica se o novo tempo de espera é maior que o mínimo permitido
                if (newTempoEspera > MIN_TEMPO_ESPERA) {
                    tempo_espera = newTempoEspera; // Atualiza o tempo de espera
                    Serial.print("Tempo de espera alterado para: ");
                    Serial.println(tempo_espera);
                } else {
                    Serial.print("O tempo de espera recebido (");
                    Serial.print(newTempoEspera);
                    Serial.println(" ms) é menor que o mínimo permitido.");
                }
            } else {
                Serial.println("O novo tempo de espera é igual ao atual. Nenhuma alteração feita.");
            }
        } else {
            Serial.println("Erro ao analisar JSON: " + String(error.c_str()));
        }
    } else {
        Serial.printf("Erro ao fazer requisição HTTP: %d\n", httpResponseCode);
    }

    http.end(); // Finaliza a conexão
}



void PZEM() {
    // Ler os dados do sensor
    voltage = pzem.voltage();
    current = pzem.current();
    power = pzem.power();
    energy = pzem.energy();
    frequency = pzem.frequency();
    pf = pzem.pf();

    // Verificar se os dados são válidos
    if (isnan(voltage)) {
        Serial.println("Erro na energização do módulo");
        sendError("Erro na energização do módulo");
    } else {
        // Imprimir os valores no console Serial
        Serial.print("Tensão (V): ");
        Serial.println(voltage);
        Serial.print("Corrente (A): ");
        Serial.println(current);
        Serial.print("Potência (W): ");
        Serial.println(power);
        Serial.print("Energia (kWh): ");
        Serial.println(energy);
        Serial.print("Frequência (Hz): ");
        Serial.println(frequency);
        Serial.print("Fator de Potência: ");
        Serial.println(pf);
        sendError("Sem Erros");
    }

    Serial.println();
}

void sendDataToMQTT() {
    if (!pzem_voltage.publish(voltage)) {
        sendError("Falha ao enviar tensão");
    }
    if (!pzem_current.publish(current)) {
        sendError("Falha ao enviar corrente");
    }
    if (!pzem_power.publish(power)) {
        sendError("Falha ao enviar potência");
    }
    if (!pzem_energy.publish(energy)) {
        sendError("Falha ao enviar energia");
    }
    if (!pzem_frequency.publish(frequency)) {
        sendError("Falha ao enviar frequência");
    }
    if (!pzem_pf.publish(pf)) {
        sendError("Falha ao enviar fator de potência");
    }
}

void sendError(const char* errorMsg) {
    Serial.println(errorMsg);
    if (!pzem_error.publish(errorMsg)) {
        Serial.println("Falha ao enviar erro");
    }
}

void connectToMQTT() {
    Serial.print("Conectando ao MQTT... ");
    while (mqtt.connect() != 0) {
        Serial.println(mqtt.connectErrorString(mqtt.connect()));
        delay(5000);
    }
    Serial.println("Conectado ao MQTT!");
}
