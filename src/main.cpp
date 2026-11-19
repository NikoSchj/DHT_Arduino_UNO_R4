//
// Created by Nikolaj on 11-11-2026.
//

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>
#include <ArduinoGraphics.h>


const uint8_t DHT_PIN = 7;                // Data eller sig på DHT
const uint32_t READ_INTERVAL_MS = 2000;   // DHT11: max omkring 1 læsning pr sekund
const int BIT_THRESHOLD_US = 40;          // HIGH længere end den = bit 1

enum class DHTstatus : uint8_t  {
    OK,
    NoResponse,      // sensoren svarede aldrig start signalet
    BitTimeout,      // sensoren stoppede i mens den sendte bits
    ChecksumError    // alle bits modtaget but dataen er korrupt
};

// Passer ikke så godt hvis det var variabler, men passer fordi de ikke kan ændre sig passer det godt
static const char *status (DHTstatus s) {
    switch (s) {
        case DHTstatus::OK: return "OK";
        case DHTstatus::NoResponse: return "Ingen respons (tjek pins)";
        case DHTstatus::BitTimeout: return "Timeout imens a der blev laest bits";  // Den tog længere tid om at udføre arbejdet end forventet
        case DHTstatus::ChecksumError: return "Der er korrupte data";
    }
    return "ukendt"; // Bliver aldrig printet, så længe s
}

// Struct der indeholder data fra DHT11 sensor
struct DHTdata {
    uint8_t ren[5];         // de 5 rene bytes, til testing
    float Fugtighed;        // %RH
    float Temperatur;       // Grader i celcius
};

static int maalNieavu(uint8_t level, uint32_t timeoutUs) {
    const uint32_t start = micros();
    while (digitalRead(DHT_PIN) == level) {
        if (micros() - start > timeoutUs) {
            return -1;
        }
    }
    return (int)(micros() - start); // her konvertere jeg unsigneded int til signed int
}

DHTstatus laesDHT(DHTdata &out) {
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // Start signalet
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN,LOW);
    delay(30); // Databladet til DHT11 kræver at pinen bliver holdt low i mindst 18 ms og maks 30 ms
    pinMode(DHT_PIN, INPUT_PULLUP);   // INPUT_PULLUP tager den tilbage til HIGH

    // Sensor respons, (venter ca 0,20-0,40 ms HIGH) -> 0,80 ms LOW -> 0,80 ms HIGH
    if (maalNieavu(HIGH, 100) < 0) return DHTstatus::NoResponse; // vent på at sensoren trækker linjen LOW
    if (maalNieavu(LOW,  100) < 0) return DHTstatus::NoResponse; // sensorens ~80 µs LOW-svar
    if (maalNieavu(HIGH, 100) < 0) return DHTstatus::NoResponse; // sensorens ~80 µs HIGH-svar

    // læs 40 bits
    for (uint8_t i = 0; i < 40; i++) {
        if (maalNieavu(LOW, 80) < 0) return DHTstatus::BitTimeout;  // Timedout

        const int highTime = maalNieavu(HIGH, 100);
        if (highTime < 0) return DHTstatus::BitTimeout;                           // Timedout

        // Tjekker, om den målte tid for den høje puls (highTime) er længere end en fastsat grænseværdi i mikrosekunder (BIT_THRESHOLD_US)
        uint8_t bit;
        if (highTime > BIT_THRESHOLD_US) {
            bit = true;
        } else {
            bit = false;
        }

        data[i / 8] <<= 1;
        data[i / 8] |= bit; // jeg rykker alle bits i currentbyte 1 bit til venstre og tænder for den sidste bit hvis biten er 0 forbliver den 0
    }

    const uint16_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    for (uint8_t i = 0; i < 5; i++) {
        out.raw[i] = data[i];
    }

    if (sum != data[4]) return DHTstatus::ChecksumError; // Der er fejl i data

    // Find fugtigheden og lig det ind i structen
    uint8_t fugt    = data[0]; // heltalsdel
    uint8_t fugtDec = data[1]; // decimaldel

    out.Fugtighed = fugt + fugtDec / 10.0f;

    // Find temperatur og lig det ind i structen
    uint8_t temp    = data[2];          // heltalsdel
    uint8_t tempDec = data[3] & 0x7F;   // decimaldel, uden fortegnsbitten

    out.Temperatur = temp + tempDec / 10.0f; // ligger temperaturen samme og laver den til en float

    // tjekker om temperaturen er negativ
    if (data[3] & 0x80) {
        out.Temperatur = -out.Temperatur;
    }

    return DHTstatus::OK;
}

// for at tjekke resultatet
static void printBinary_tjek(uint8_t value) {
    for (int8_t bit = 7; bit >= 0; bit--) {
        Serial.print((value >> bit) & 0x01);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) {
        // vent i op til 5 sekunder på serial forbindelse
    }
    pinMode(DHT_PIN, INPUT_PULLUP);
    delay(1000);   // DHT11 har brug for 1 sek efter start up før den kan læse
    Serial.println("DHT11 startede");
}

void loop() {
    static uint32_t sidst_laest = 0;

    if (millis() - sidst_laest < READ_INTERVAL_MS) {
        return;
    }
    sidst_læst = millis();

    DHTdata reading{};
    const DHTstatus status = laesDHT(reading);

    // Tjek
    Serial.print("Ren: ");
    for (uint8_t i = 0; i < 5; i++) {
        printBinary_tjek(reading.ren[i]);
        Serial.print(' ');
    }
    Serial.print(" -> ");

    // find temperaturen

    // find luftfugtighede


}
