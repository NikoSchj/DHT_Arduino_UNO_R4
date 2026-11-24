//
// Created by Nikolaj on 11-11-2026.
//

#include <Arduino.h>
#include <Arduino_LED_Matrix.h>
#include <ArduinoGraphics.h>

const int DHT_PIN = 7;                // 'Data' eller 'sig' på DHT. DHT11's DATA-pin er forbundet til D7
const uint32_t LAES_INTERVAL_MS = 2000;   // DHT11: max omkring 1 læsning pr sekund, står i databladet
const uint8_t BIT_THRESHOLD_MCS = 40;     // HIGH længere end den = bit 1
const int KNAP_PIN = 8;                   // Pinnen til knappen
volatile bool visTemp = true;             // Til knappens stadie
volatile uint32_t sidstTrykket = 0;

void knapValg() {
    uint32_t nu = millis();
    if (nu - sidstTrykket > 200) {
        visTemp = !visTemp;
        sidstTrykket = nu;
    }
}

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
    uint8_t bytes[5];
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
    return (int)(micros() - start); // konverterer uint32_t (unsigned) til int (signed), så den kan returnere -1 ved fejl
}

// funktionen der køre
static DHTstatus laesDHT(DHTdata &out) {
    uint8_t data[5] = {0, 0, 0, 0, 0};

    // Start signalet
    pinMode(DHT_PIN, OUTPUT);
    digitalWrite(DHT_PIN,LOW);
    delay(20); // Databladet til DHT11 kræver at pinen bliver holdt low i mindst 18 ms og maks 30 ms
    pinMode(DHT_PIN, INPUT_PULLUP);   // INPUT_PULLUP tager den tilbage til HIGH

    // Sensor respons, (venter ca 20-40 mikro sekunder HIGH) -> 80 mikro sekunder LOW -> 80 mikro sekunder HIGH
    if (maalNieavu(HIGH, 100) < 0) {
        return DHTstatus::NoResponse; // vent på at sensoren trækker linjen LOW
    }
    if (maalNieavu(LOW,  100) < 0) {
        return DHTstatus::NoResponse; // sensorens 80 mikro sekunder LOW-svar
    }
    if (maalNieavu(HIGH, 100) < 0) {
        return DHTstatus::NoResponse; // sensorens 80 mikro sekunder HIGH-svar
    }

    // læs 40 bits
    for (uint8_t i = 0; i < 40; i++) {
        if (maalNieavu(LOW, 80) < 0) {
            return DHTstatus::BitTimeout;  // Timedout
        }


        const int highTid = maalNieavu(HIGH, 100);
        if (highTid < 0) {
            return DHTstatus::BitTimeout; // Timedout
        }

        // Tjekker, om den målte tid for den høje puls (highTid) er længere end en fastsat grænseværdi i mikrosekunder (BIT_THRESHOLD_US)
        // uint8_t er samme størelse som 1 byte eller 8 bit, hvilket er det jeg for svaret i fra måleren
        uint8_t bit;
        if (highTid > BIT_THRESHOLD_MCS) {
            bit = true;
        } else {
            bit = false;
        }

        /* jeg rykker alle bits i data 1 bit til venstre og tænder for den sidste bit,
         * hvis biten er 0 forbliver den 0 */
        data[i / 8] <<= 1;
        data[i / 8] |= bit;
    }

    // I datasheetet står der hvordan den giver data temp, humidity og cheksum data
    const uint16_t sum = (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    for (uint8_t i = 0; i < 5; i++) {
        out.bytes[i] = data[i];
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

// For at tjekke resultatet fra sensoren
void printBinary_tjek(uint8_t value) {
    for (int8_t bit = 7; bit >= 0; bit--) {
        Serial.print((value >> bit) &0x01); // Print værdien (0/1) af bit nr. 'bit' i value
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 5000) {
        // vent i op til 5 sekunder på serial forbindelse
    }
    pinMode(KNAP_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(KNAP_PIN), knapValg, FALLING);
    // DHT11 sensor start up
    pinMode(DHT_PIN, INPUT_PULLUP);
    delay(1000);   // sensor har brug for 1 sek efter start up før den kan læse
    Serial.println("DHT11 startede!");
    Serial.println("Starter med at vise temperatur, brug knappen til at skifte mellem temperatur og fugtighed");
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
    Serial.println("Bits: ");
    for (uint8_t i = 0; i < 5; i++) {
        printBinary_tjek(reading.bytes[i]);
        Serial.print(' ');
    }
    Serial.println(" -> ");

    // Læser status for sensoren, hvis dens status ikke er OK printer den status for at fortælle hvad der er forkert
    if (laestStatus != DHTstatus::OK) {
        Serial.print(status(laestStatus));
        return;
    }

    if (visTemp) {
        Serial.print("Viser temperatur målinger\r\n");
        // find temperaturen
        Serial.print("Temperatur: ");
        Serial.print(reading.Temperatur);
        Serial.print(" C\r\n");
    } else {
        Serial.print("Viser fugtigheds målinger\r\n");
        //En DHT11 måler luftfugtighed ved at registrere ændringer i den elektriske modstand i et fugtabsorberende materiale
        // find luftfugtighede
        Serial.print("Fugtighed: ");
        Serial.print(reading.Fugtighed);
        Serial.print(" %RH\r\n"); // RH står for relative humidity, og måler procent af vanddamp i luften
    }

}
