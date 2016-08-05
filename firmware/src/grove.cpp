#include "grove.h"

void setup() { 
    leds.begin();
    leds.show();
    randomSeed(analogRead(0));

    pinMode (slaveSelectPin, OUTPUT);
    SPI.begin(); 

    // Serial.begin(9600); // USB is always 12 Mbit/sec
    
    for (uint8_t c=1; c <= 12; c++) {
        dispatcher(c, 0);
    }    
    
    // This has the effect of running through a pulsating red, then green, then blue. Used to test dispatcher.
    for (uint8_t chan_group=0; chan_group < 4; chan_group++) {
        // Raise light for single colors at a time (in channel groups)
        for (uint8_t i=0; i < 255; i++) {
            for (uint8_t c=1; c <= 12; c++) {
                if ((c-1) % 3 == chan_group) { // All the reds, then the greens, then the blues
                    dispatcher(c, i);
                }
            }
            delay(1);
        }
        // Lower light
        for (uint8_t i=255; i > 0; i--) {
            for (uint8_t c=1; c <= 12; c++) {
                if ((c-1) % 3 == chan_group) {
                    dispatcher(c, i);
                }
            }
            delay(1);
        }
    }
}

void loop() {
    addRandomDrip();
    advanceRestDrips();
    
    if (millis() % 60000 < 15000) {
        addBreath();
    }
    advanceBreaths();
    
    runLeaves();
    
    leds.show();    
}

/**
 * Set the analog output on the dispatcher board.
 * Channels are numbered 1 to 12 inclusive (THEY DON'T START AT ZERO !!) 
 * value is 0-255 range analog output of the dac/LED brightness
 */
void dispatcher(uint8_t chan, uint8_t value) {
    // take the SS pin low to select the chip:
    SPI.beginTransaction(dispatcherSPISettings);

    digitalWrite(slaveSelectPin, LOW);
    //  send in the address and value via SPI:
    SPI.transfer(flipByte((chan & 0x0F) << 4));
    SPI.transfer(value);
    // take the SS pin high to de-select the chip:
    digitalWrite(slaveSelectPin,HIGH);

    SPI.endTransaction();
}

uint8_t flipByte(uint8_t val) {
    uint8_t result = 0;
    for (uint8_t i = 0; i < 8; i++) {
        result |= ((val & ((0x01) << i)) >> i) << (7-i);
    }
    return result;
}

void addRandomDrip() {
    // Wait until drip is far enough away from beginning
    float progress = 0;
    int d = dripCount % DRIP_LIMIT;
    
    if (dripCount) {
        int latestDripIndex = d - 1;
        if (latestDripIndex < 0) latestDripIndex = DRIP_LIMIT - 1; // wrap around
        int latestDripStart = dripStarts[latestDripIndex];
        progress = (millis() - latestDripStart) / float(REST_DRIP_TRIP_MS);
        
        // Don't start a new drip if the latest drip isn't done coming out
        if (progress <= ((REST_DRIP_WIDTH_MAX+REST_DRIP_WIDTH_MIN)/float(ledsPerStrip))) {
            // Serial.println(" ---> Not starting drip, last not done yet.");
            return;
        }
    }

    // Serial.print(" ---> Drip progress: ");
    // Serial.print(progress);
    // Serial.print(" -- ");
    // Serial.print(ceil(progress * ledsPerStrip));
    // Serial.print(" < ");
    // Serial.println(furthestBreathPosition);
    
    if (!dripCount || random(0, (250 * max(1.f - progress, 1))) <= 1) {
        dripStarts[d] = millis();
        dripColors[d] = randomGreen();
        dripWidth[d] = random(REST_DRIP_WIDTH_MIN, REST_DRIP_WIDTH_MAX);
        dripEaten[d] = false;
        dripCount++;
    }
}

bool hasNewBreath() {
    if (activeBreath != -1) return false;
    
    if (millis() > lastNewBreathMs) {
        endActiveBreathMs = millis() + random(300, 1250);
        lastNewBreathMs = endActiveBreathMs + random(1000, 3000);
        return true;
    }
    
    return false;
}

bool hasActiveBreath() {
    if (activeBreath == -1) return false;
    
    if (millis() < endActiveBreathMs) {
        return true;
    }
    
    activeBreath = -1;
    return false;
}

void addBreath() {
    int b = breathCount % BREATH_LIMIT;
    bool newBreath = hasNewBreath();
    
    if (newBreath) {
        // Serial.print(" ---> New breath: ");
        // Serial.println(b);
        breathStarts[b] = millis();
        breathPosition[b] = 0;
        breathWidth[b] = 1;
        breathCount++;
        activeBreath = breathCount % BREATH_LIMIT;
    }

    if (!newBreath && hasActiveBreath()) {
        // Still breathing, so extend breath
        int latestBreathIndex = b - 1;
        if (latestBreathIndex < 0) latestBreathIndex = BREATH_LIMIT - 1; // wrap around
        int latestBreathStart = breathStarts[latestBreathIndex];
        float progress = float(millis() - latestBreathStart) / float(BREATH_TRIP_MS);
        
        breathWidth[latestBreathIndex] = ceil(progress * ledsPerStrip);
        
        // Serial.print(" ---> Active breath #");
        // Serial.print(latestBreathIndex);
        // Serial.print(": ");
        // Serial.print(breathWidth[latestBreathIndex]);
        // Serial.print(": ");
    } else if (!newBreath) {
        // Not actively breathing
    }
    
}

void advanceRestDrips() {
    for (unsigned int d=0; d < min(dripCount, DRIP_LIMIT); d++) {
        unsigned int dripStart = dripStarts[d];
        // The 1.1 is to ensure that the tail of each drip disappears, since
        // this only considers the position of the head of the drip.
        float progress = (millis() - dripStart) / float(REST_DRIP_TRIP_MS);
        if (progress >= 1.1) continue;
        if (dripEaten[d]) continue;
        unsigned int dripColor = dripColors[d];

        drawDrip(d, dripStart, dripColor);
    }
}

void advanceBreaths() {
    for (unsigned int b=0; b < min(breathCount, BREATH_LIMIT); b++) {
        unsigned int breathStart = breathStarts[b];
        float progress = (millis() - breathStart) / float(BREATH_TRIP_MS);
        if (progress >= 1.1) continue;
        drawBreath(b, breathStart);
    }
}

void drawDrip(int d, int dripStart, int dripColor) {
    float progress = (millis() - dripStart) / float(REST_DRIP_TRIP_MS);
    int head = 0;
    int tail = dripWidth[d];
    double currentLed = 0;
    double headFractional = modf(progress * ledsPerStrip, &currentLed);
    double tailFractional = (1 - REST_DRIP_DECAY) * (1 - headFractional);
    currentLed = ledsPerStrip - currentLed;
    
    int baseColor = dripColor;
    int color;

    // Serial.print(" ---> Drip #");
    // Serial.print(d);
    // Serial.print(": ");
    // Serial.print(currentLed);
    // Serial.print(" < ");
    // Serial.println(furthestBreathPosition);

    for (int i=head; i <= tail; i++) {
        // Head and tail pixel is the fractional fader
        color = baseColor;
        double tailDecay = 1 - REST_DRIP_DECAY*i/tail;
        uint8_t r = ((color & 0xFF0000) >> 16) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        uint8_t g = ((color & 0x00FF00) >> 8) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        uint8_t b = ((color & 0x0000FF) >> 0) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        color = ((r<<16)&0xFF0000) | ((g<<8)&0x00FF00) | ((b<<0)&0x0000FF);

        if (currentLed+i < furthestBreathPosition) {
            continue;
        }
        leds.setPixel(ledsPerStrip*2+currentLed+i, color);
    }
    
    if (currentLed+tail < furthestBreathPosition) {
        dripEaten[d] = true;
    }
    for (int i=1; i <= 5; i++) {
        leds.setPixel(ledsPerStrip*2+currentLed+tail+i, 0);
    }
}

void drawBreath(int b, int breathStart) {
    float progress = (millis() - breathStart) / float(BREATH_TRIP_MS);
    int head = 0;
    int tail = breathWidth[b];
    double currentLed = 0;
    double headFractional = modf(progress * ledsPerStrip, &currentLed);
    double tailFractional = (1 - BREATH_DECAY) * (1 - headFractional);
    breathPosition[b] = currentLed;
    if (currentLed-tail > ledsPerStrip) {
        furthestBreathPosition = 0;
    } else if (currentLed > furthestBreathPosition) {
        furthestBreathPosition = breathPosition[b];
    }

    // Serial.print(" ---> Breath #");
    // Serial.print(b);
    // Serial.print(": ");
    // Serial.print(currentLed);
    // Serial.print(" >? ");
    // Serial.println(furthestBreathPosition);
    
    int baseColor = 0xFFFFFF;
    int color;
    // Serial.print(" ---> Breath #");
    // Serial.print(b);
    // Serial.print(": ");
    // Serial.print(currentLed);
    // Serial.print(" -> ");
    // Serial.println(tail);
    
    for (int i=head; i <= tail; i++) {
        // Head and tail pixel is the fractional fader
        color = baseColor;
        double tailDecay = 1 - BREATH_DECAY*i/tail;
        uint8_t r = ((color & 0x020000) >> 16) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        uint8_t g = ((color & 0x000200) >> 8) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        uint8_t b = ((color & 0x000036) >> 0) * (i == head ? headFractional : i == tail ? tailFractional : tailDecay);
        color = ((r<<16)&0xFF0000) | ((g<<8)&0x00FF00) | ((b<<0)&0x0000FF);

        leds.setPixel(ledsPerStrip*2+currentLed-i, color);
    }
    
    if (currentLed - tail > ledsPerStrip) {
        breathStarts[b] = 0;
    }
    
    for (int i=1; i <= 5; i++) {
        leds.setPixel(ledsPerStrip*2+currentLed-tail-i, 0);
    }
}

void runLeaves() {
    float boostDuration = 500;
    
    for (int c=1; c <= 12; c++) {
        int channelOffset = millis() + ((c<=4 ? 0 : c <= 8 ? 1 : c <= 12 ? 2 : 3) * LEAVES_REST_MS/4);
        float multiplier = (channelOffset % LEAVES_REST_MS) / float(LEAVES_REST_MS);
        int sinPos = multiplier * 360.0f;
        float progress = sinTable[sinPos];
        int center = 100;
        int width = 35;
        int brightness = center + width*progress;

        
        if (dripCount) {
            int d = dripCount % DRIP_LIMIT;
            int latestDripIndex = d - 1;
            if (latestDripIndex < 0) latestDripIndex = DRIP_LIMIT - 1; // wrap around
            float boost = 255 - center - width;
            if (millis() - dripStarts[latestDripIndex] < boostDuration) {
                // Linear boost up to max brightness
                progress = ((millis() - dripStarts[latestDripIndex]) / boostDuration);
                boost = boost * progress;
                brightness += boost;
            } else if (millis() - dripStarts[latestDripIndex] - boostDuration < boostDuration) {
                // Hold boost
                brightness += boost;
            } else if (millis() - dripStarts[latestDripIndex] - 2*boostDuration < boostDuration) {
                // Linear boost down back to baseline
                progress = ((millis() - dripStarts[latestDripIndex] - 2*boostDuration) / boostDuration);
                boost = boost * (1 - progress);
                brightness += boost;
            }
        }

        // Serial.print(" ---> Leaves: ");
        // Serial.print(brightness);
        // Serial.print(" ");
        // Serial.println(")");
        
        dispatcher(c, (c-1)%3==0 ? brightness : 0);
    }
    
    // Serial.println("");
}

int randomGreen() {
    int greens[] = {
        0x36CD00, // Light green
        0x06FD00, // Pure green
        0x06CD00, // Bright green
        0x8CCC00, // Green-Yellowish
        0xACEC00, // Yellowish-Green
        0x45DB06, // Teal green
        0x29BD06  // Seafoam green
    };

    return greens[random(0, sizeof(greens)/sizeof(int))];
}
