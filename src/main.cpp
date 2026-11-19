//
// Created by Nikolaj on 11-11-2026.
//

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>
#include <ArduinoGraphics.h>


const uint8_t DHT_PIN = 7;                // 'Data' eller 'sig' på DHT. DHT11's DATA-pin er forbundet til D7
const uint32_t LAES_INTERVAL_MS = 2000;   // DHT11: max omkring 1 læsning pr sekund, står i databladet
const int BIT_THRESHOLD_MCS = 40;         // HIGH længere end den = bit 1

// Værdierne inde i enum gemmes som unsigned int af 8 bits, dvs hver fylder en byte og bliver numeret som {0, 1, 2, 3}
enum class DHTstatus : uint8_t  {
    OK,              // Sensoren køre fint
    NoResponse,      // sensoren svarede aldrig start signalet
    BitTimeout,      // sensoren stoppede i mens den sendte bits
    ChecksumError    // alle bits modtaget but dataen er korrupt
};

// Passer ikke så godt hvis det var variabler, men passer fordi de ikke kan ændre sig passer det godt
static const char *status (DHTstatus s) {
    switch (s) {
        case DHTstatus::OK : return "OK";                                         // værdien OK, som hører til DHTstatus
        case DHTstatus::NoResponse : return "Ingen respons (tjek pins)";
        case DHTstatus::BitTimeout : return "Timeout imens der blev laest bits";  // Den tog længere tid om at udføre arbejdet end forventet
        case DHTstatus::ChecksumError : return "Der er korrupte data";
    }
    return "ukendt"; // Nås kun hvis enum får en ny værdi, som switch case statmentet ikke dækker
}

// Struct der indeholder data fra DHT11 sensor
struct DHTdata {
    uint8_t ren[5];         // de 5 rene bytes, til testing
    float Fugtighed;        // %RH, betyder Relative Humidity
    float Temperatur;       // Grader i celcius
};

// Fungere som et stopur, starter tid, og returnere hvor langtid level blev på enten high eller low
static int maalNieavu(bool level, uint32_t timeoutMcs) {

    //
    const uint32_t start = micros();
    while (digitalRead(DHT_PIN) == level) {
        if (micros() - start > timeoutMcs) {
            return -1;
        }
    }
    return (int)(micros() - start); // konverterer uint32_t (unsigned) til int (signed), så funktionen kan returnere -1 ved fejl
}

static DHTstatus laesDHT(DHTdata &out) {
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // Start signalet
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN,LOW);
    delay(20); // Databladet til DHT11 kræver at pinen bliver holdt low i mindst 18 ms og maks 30 ms
    pinMode(DHT_PIN, INPUT_PULLUP);   // INPUT_PULLUP tager den tilbage til HIGH

    // Sensor respons, (venter ca 20-40 mikro sekunder HIGH) -> 80 mikro sekunder LOW -> 80 mikro sekunder HIGH
    if (maalNieavu(HIGH, 100) < 0) return DHTstatus::NoResponse; // vent på at sensoren trækker linjen LOW
    if (maalNieavu(LOW,  100) < 0) return DHTstatus::NoResponse; // sensorens 80 mikro ssekunder LOW-svar
    if (maalNieavu(HIGH, 100) < 0) return DHTstatus::NoResponse; // sensorens 80 mikro sekunder HIGH-svar

    // læs 40 bits
    for (uint8_t i = 0; i < 40; i++) {
        if (maalNieavu(LOW, 80) < 0) return DHTstatus::BitTimeout;  // Timedout


        const int highTid = maalNieavu(HIGH, 100);
        if (highTid < 0) return DHTstatus::BitTimeout;                           // Timedout

        // Tjekker, om den målte tid for den høje puls (highTid) er længere end en fastsat grænseværdi i mikrosekunder (BIT_THRESHOLD_US)
        uint8_t bit;
        if (highTid > BIT_THRESHOLD_MCS) {
            bit = true;
        } else {
            bit = false;
        }

        data[i / 8] <<= 1;
        data[i / 8] |= bit; // jeg rykker alle bits i currentbyte 1 bit til venstre og tænder for den sidste bit, hvis biten er 0 forbliver den 0
    }

    // I datasheetet står der hvordan den giver data temp, humidity og cheksum data
    const uint16_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    for (uint8_t i = 0; i < 5; i++) {
        out.ren[i] = data[i];
    }

    /* Datasheetet for DHT11 beskriver at dataene bliver givet som en heltals og en decimal del
     * for både fugtighed og temperatur.
     * DHT11'ens data format er følgende 8bit integral RH data + 8bit decimal RH data + 8bit integral T data
     * + 8bit decimal T data + 8bit check sum */
    if (sum != data[4]) return DHTstatus::ChecksumError; // Tjekker om checksum dataen er der, hvis ikke så er der fejl i dataen

    // Find fugtigheden og lig det ind i structen
    uint8_t fugt = data[0];    // heltalsdel
    uint8_t fugtDec = data[1]; // decimaldel

    out.Fugtighed = fugt + fugtDec / 10.0f;

    // Find temperatur og lig det ind i structen
    uint8_t temp = data[2];             // heltalsdel
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

    if (millis() - sidst_laest < LAES_INTERVAL_MS) {
        return;
    }
    sidst_laest = millis();

    DHTdata reading = {0}; // sætter alle værdier i structen til 0 så den køre om igen
    const DHTstatus laestStatus = laesDHT(reading);

    // Tjek
    Serial.println("Ren: ");
    for (uint8_t i = 0; i < 5; i++) {
        printBinary_tjek(reading.ren[i]);
        Serial.println(' '); // Starter en ny linje
    }
    Serial.println(" -> ");

    // Læser status for DHT11, hvis dens status ikke er OK printer den status for at fortælle hvad der er forkert
    if (laestStatus != DHTstatus::OK) {
        Serial.print(status(laestStatus));
        return;
    }

    // find temperaturen
    Serial.print("Temperatur: ");
    Serial.print(reading.Temperatur);
    Serial.print(" C\r\n");

    //En DHT11 måler luftfugtighed ved at registrere ændringer i den elektriske modstand i et fugtabsorberende materiale
    // find luftfugtighede
    Serial.print("Fugtighed: ");
    Serial.print(reading.Fugtighed);
    Serial.print(" %RH\r\n"); // RH står for relative humidity, og måler procent af vanddamp i luften

}
