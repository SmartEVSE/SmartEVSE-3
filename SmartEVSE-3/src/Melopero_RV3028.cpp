#if SMARTEVSE_VERSION >= 40
#include "Melopero_RV3028.h"

Melopero_RV3028::Melopero_RV3028(){
}

void Melopero_RV3028::initI2C(TwoWire &bus){
    i2c = &bus;
}

uint8_t Melopero_RV3028::readFromRegister(uint8_t registerAddress){
    i2c->beginTransmission(RV3028_ADDRESS);

    //set register pointer
    i2c->write(registerAddress);
    i2c->endTransmission();

    i2c->requestFrom(RV3028_ADDRESS, (uint8_t)1);
    //TODO: check if the byte is sent with i2c->available()
    uint8_t result = i2c->read();
    return result;
}

void Melopero_RV3028::writeToRegister(uint8_t registerAddress, uint8_t value){
    i2c->beginTransmission(RV3028_ADDRESS);
    //set register pointer
    i2c->write(registerAddress);
    i2c->write(value);
    i2c->endTransmission();
}

void Melopero_RV3028::writeToRegisters(uint8_t startAddress, uint8_t *values, uint8_t length){
    i2c->beginTransmission(RV3028_ADDRESS);
    //set start register address
    i2c->write(startAddress);

    i2c->write(values, length);

    i2c->endTransmission();
}

void Melopero_RV3028::andOrRegister(uint8_t registerAddress, uint8_t andValue, uint8_t orValue){
    uint8_t regValue = readFromRegister(registerAddress);
    regValue &= andValue;
    regValue |= orValue;
    writeToRegister(registerAddress, regValue);
}

uint8_t Melopero_RV3028::BCDtoDEC(uint8_t bcd){
    return (bcd / 0x10 * 10) + (bcd % 0x10);
}

uint8_t Melopero_RV3028::DECtoBCD(uint8_t dec){
    return ((dec / 10) << 4) ^ (dec % 10);
}

uint8_t Melopero_RV3028::to12HourFormat(uint8_t bcdHours){
    //PM HOURS bit 5 is 1
    if (BCDtoDEC(bcdHours) > 12){
        bcdHours = DECtoBCD(BCDtoDEC(bcdHours) - 12);
        bcdHours |= PM_FLAG;
    }
    return bcdHours;
}

uint8_t Melopero_RV3028::to24HourFormat(uint8_t bcdHours){
    if((bcdHours & PM_FLAG) > 0)
        bcdHours = DECtoBCD(BCDtoDEC(bcdHours & ~PM_FLAG) + 12);

    return bcdHours;
}

bool Melopero_RV3028::is12HourMode(){
    return (readFromRegister(CONTROL2_REGISTER_ADDRESS) & AMPM_HOUR_FLAG) > 0;
}

void Melopero_RV3028::set12HourMode(){
    //avoid converting alarm hours if they are already in 12 hour format
    if (!is12HourMode()){
        uint8_t alarmHours = readFromRegister(HOURS_ALARM_REGISTER_ADDRESS) & HOURS_ONLY_FILTER_FOR_ALARM;
        writeToRegister(HOURS_ALARM_REGISTER_ADDRESS, to12HourFormat(alarmHours));
    }

    writeToRegister(CONTROL2_REGISTER_ADDRESS, (readFromRegister(CONTROL2_REGISTER_ADDRESS) | AMPM_HOUR_FLAG));
}

void Melopero_RV3028::set24HourMode(){
    //avoid converting alarm hours if they are already in 24 hour format
    if (is12HourMode()){
        uint8_t alarmHours = readFromRegister(HOURS_ALARM_REGISTER_ADDRESS) & HOURS_ONLY_FILTER_FOR_ALARM;
        writeToRegister(HOURS_ALARM_REGISTER_ADDRESS, to24HourFormat(alarmHours));
    }

    writeToRegister(CONTROL2_REGISTER_ADDRESS, (readFromRegister(CONTROL2_REGISTER_ADDRESS) & (~AMPM_HOUR_FLAG)));
}

void Melopero_RV3028::setTime(uint16_t year, uint8_t month, uint8_t weekday, uint8_t date, uint8_t hour, uint8_t minute, uint8_t second){
    uint8_t time_array[] = {DECtoBCD(second), DECtoBCD(minute), DECtoBCD(hour), DECtoBCD(weekday), DECtoBCD(date), DECtoBCD(month), DECtoBCD((uint8_t) (year - 2000))};

    if (is12HourMode()){
        set24HourMode();
        writeToRegisters(SECONDS_REGISTER_ADDRESS, time_array, 7);
        set12HourMode();
    }
    else{
        writeToRegisters(SECONDS_REGISTER_ADDRESS, time_array, 7);
    }

}


uint8_t Melopero_RV3028::getTSSecond(){
    return BCDtoDEC(readFromRegister(SECONDS_TS_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getTSMinute(){
    return BCDtoDEC(readFromRegister(MINUTES_TS_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getTSHour(){
    if (is12HourMode()){
        uint8_t bcdHours = readFromRegister(HOURS_TS_REGISTER_ADDRESS);
        return BCDtoDEC(bcdHours & ~PM_FLAG);
    }
    else {
        return BCDtoDEC(readFromRegister(HOURS_TS_REGISTER_ADDRESS));
    }
}

uint8_t Melopero_RV3028::getTSDate(){
    return BCDtoDEC(readFromRegister(DATE_TS_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getTSMonth(){
    return BCDtoDEC(readFromRegister(MONTH_TS_REGISTER_ADDRESS));
}

uint16_t Melopero_RV3028::getTSYear(){
    return BCDtoDEC(readFromRegister(YEAR_TS_REGISTER_ADDRESS)) + 2000;
}



uint8_t Melopero_RV3028::getSecond(){
    return BCDtoDEC(readFromRegister(SECONDS_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getMinute(){
    return BCDtoDEC(readFromRegister(MINUTES_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getHour(){
    if (is12HourMode()){
        uint8_t bcdHours = readFromRegister(HOURS_REGISTER_ADDRESS);
        return BCDtoDEC(bcdHours & ~PM_FLAG);
    }
    else {
        return BCDtoDEC(readFromRegister(HOURS_REGISTER_ADDRESS));
    }
}


bool Melopero_RV3028::isPM(){
    return (readFromRegister(HOURS_REGISTER_ADDRESS) & PM_FLAG) > 0;
}

uint8_t Melopero_RV3028::getWeekday(){
    return BCDtoDEC(readFromRegister(WEEKDAY_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getDate(){
    return BCDtoDEC(readFromRegister(DATE_REGISTER_ADDRESS));
}

uint8_t Melopero_RV3028::getMonth(){
    return BCDtoDEC(readFromRegister(MONTH_REGISTER_ADDRESS));
}

uint16_t Melopero_RV3028::getYear(){
    return BCDtoDEC(readFromRegister(YEAR_REGISTER_ADDRESS)) + 2000;
}

uint32_t Melopero_RV3028::getUnixTime(){
    uint32_t unixTime = 0;
    unixTime |= ((uint32_t) readFromRegister(UNIX_TIME_ADDRESS));
    unixTime |= ((uint32_t) readFromRegister(UNIX_TIME_ADDRESS + 1)) << 8;
    unixTime |= ((uint32_t) readFromRegister(UNIX_TIME_ADDRESS + 2)) << 16;
    unixTime |= ((uint32_t) readFromRegister(UNIX_TIME_ADDRESS + 3)) << 24;
    return unixTime;
}

bool Melopero_RV3028::isDateModeForAlarm(){
    return (readFromRegister(CONTROL1_REGISTER_ADDRESS) & DATE_ALARM_MODE_FLAG) > 0;
}

void Melopero_RV3028::setDateModeForAlarm(bool flag){
    if (flag){
        writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) | DATE_ALARM_MODE_FLAG);
    }
    else {
        writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) & ~DATE_ALARM_MODE_FLAG);
    }
}

void Melopero_RV3028::enableAlarm(uint8_t weekdayOrDate, uint8_t hour, uint8_t minute,bool dateAlarm, bool hourAlarm, bool minuteAlarm, bool generateInterrupt){
    //1. Initialize bits AIE and AF to 0.
    //AF
    uint8_t reg_value = readFromRegister(STATUS_REGISTER_ADDRESS) & ~ALARM_FLAG;
    writeToRegister(STATUS_REGISTER_ADDRESS, reg_value);
    //AIE
    reg_value = readFromRegister(CONTROL2_REGISTER_ADDRESS) & ~ALARM_INTERRUPT_FLAG;
    writeToRegister(CONTROL2_REGISTER_ADDRESS, reg_value);

    if (generateInterrupt){
        reg_value = readFromRegister(CONTROL2_REGISTER_ADDRESS) | ALARM_INTERRUPT_FLAG;
        writeToRegister(CONTROL2_REGISTER_ADDRESS, reg_value);
    }

    //2. Alarm settings
    if (dateAlarm)
        writeToRegister(WEEKDAY_DATE_ALARM_REGISTER_ADDRESS, DECtoBCD(weekdayOrDate) & ENABLE_ALARM_FLAG);
    else
        writeToRegister(WEEKDAY_DATE_ALARM_REGISTER_ADDRESS, DECtoBCD(weekdayOrDate) | ~ENABLE_ALARM_FLAG);

    if (hourAlarm)
        writeToRegister(HOURS_ALARM_REGISTER_ADDRESS, DECtoBCD(hour) & ENABLE_ALARM_FLAG);
    else
        writeToRegister(HOURS_ALARM_REGISTER_ADDRESS, DECtoBCD(hour) | ~ENABLE_ALARM_FLAG);

    if (minuteAlarm)
        writeToRegister(MINUTES_ALARM_REGISTER_ADDRESS, DECtoBCD(minute) & ENABLE_ALARM_FLAG);
    else
        writeToRegister(MINUTES_ALARM_REGISTER_ADDRESS, DECtoBCD(minute) | ~ENABLE_ALARM_FLAG);
}

void Melopero_RV3028::disableAlarm(){
    writeToRegister(WEEKDAY_DATE_ALARM_REGISTER_ADDRESS, ~ENABLE_ALARM_FLAG);
    writeToRegister(HOURS_ALARM_REGISTER_ADDRESS, ~ENABLE_ALARM_FLAG);
    writeToRegister(MINUTES_ALARM_REGISTER_ADDRESS, ~ENABLE_ALARM_FLAG);
}

void Melopero_RV3028::enablePeriodicTimer(uint16_t ticks, TimerClockFrequency freq, bool repeat, bool generateInterrupt){
    //1. set TE, TIE, TF to 0
    //TE
    writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) & ~TIMER_ENABLE_FLAG);
    //TIE
    writeToRegister(CONTROL2_REGISTER_ADDRESS, readFromRegister(CONTROL2_REGISTER_ADDRESS) & ~TIMER_INTERRUPT_ENABLE_FLAG);
    //TF
    writeToRegister(STATUS_REGISTER_ADDRESS, readFromRegister(STATUS_REGISTER_ADDRESS) & ~TIMER_EVENT_FLAG);

    //2. TRPT to 1 for repeat mode and select freq
    if (repeat)
        writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) | TIMER_REPEAT_FLAG | freq);
    else
        writeToRegister(CONTROL1_REGISTER_ADDRESS, (readFromRegister(CONTROL1_REGISTER_ADDRESS) & ~TIMER_REPEAT_FLAG) | freq);

    //set countdown
    uint8_t lsb = (uint8_t) ticks;
    uint8_t msb = (ticks >> 8) & 0x0F;

    writeToRegister(TIMER_VALUE_0_ADDRESS, lsb);
    writeToRegister(TIMER_VALUE_1_ADDRESS, msb);

    //hardware interrupt
    if (generateInterrupt)
        writeToRegister(CONTROL2_REGISTER_ADDRESS, readFromRegister(CONTROL2_REGISTER_ADDRESS) | TIMER_INTERRUPT_ENABLE_FLAG);
    else
        writeToRegister(CONTROL2_REGISTER_ADDRESS, readFromRegister(CONTROL2_REGISTER_ADDRESS) & ~TIMER_INTERRUPT_ENABLE_FLAG);

    //enable timer
    writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) | TIMER_ENABLE_FLAG);

}

void Melopero_RV3028::disablePeriodicTimer(){
    writeToRegister(CONTROL1_REGISTER_ADDRESS, readFromRegister(CONTROL1_REGISTER_ADDRESS) & ~TIMER_ENABLE_FLAG);
}

void Melopero_RV3028::enablePeriodicTimeUpdate(bool everySecond, bool generateInterrupt){
    uint8_t orValue = everySecond ? 0 : 0x10;
    andOrRegister(CONTROL1_REGISTER_ADDRESS, 0xFF, orValue);
    if (generateInterrupt) 
        andOrRegister(CONTROL2_REGISTER_ADDRESS, 0xFF, 0x20);
    else 
        andOrRegister(CONTROL2_REGISTER_ADDRESS, 0xDF, 0);
}

void Melopero_RV3028::disablePeriodicTimeUpdate(){
    enablePeriodicTimeUpdate(true, false);
}

void Melopero_RV3028::clearInterruptFlags(bool clearTimerFlag, bool clearAlarmFlag, bool clearPeriodicTimeUpdateFlag){
    uint8_t mask = 0xFF;
    mask = clearTimerFlag ? mask & 0xF7 : mask;
    mask = clearAlarmFlag ? mask & 0xFB : mask;
    mask = clearPeriodicTimeUpdateFlag ? mask & 0xEF : mask;
    andOrRegister(STATUS_REGISTER_ADDRESS, mask, 0);
}

bool Melopero_RV3028::waitforEEPROM(){
	unsigned long timeout = millis() + 500;
	while ((readFromRegister(STATUS_REGISTER_ADDRESS) & 0x80) && millis() < timeout);
	return millis() < timeout;
}

// Sets up the device to read/write from/to the eeprom memory. The automatic refresh function has to be disabled.
void Melopero_RV3028::useEEPROM(bool disableRefresh){
    if (disableRefresh){
        andOrRegister(CONTROL1_REGISTER_ADDRESS, 0xFF, 0x08);
        writeToRegister(EEPROM_COMMAND_ADDRESS, 0);
    }
    else
        andOrRegister(CONTROL1_REGISTER_ADDRESS, 0xF7, 0);
}


/* Reads an eeprom register and returns its content.
 * user eeprom address space : [0x00 - 0x2A]
 * configuration eeprom address space : [0x30 - 0x37] */
uint8_t Melopero_RV3028::readEEPROMRegister(uint8_t registerAddress){
    writeToRegister(EEPROM_ADDRESS_ADDRESS, registerAddress);

    // read a register -> eeprom data = 0x00 -> eeprom data = 0x22
    writeToRegister(EEPROM_COMMAND_ADDRESS, 0x00);
    writeToRegister(EEPROM_COMMAND_ADDRESS, 0x22);
    if (!waitforEEPROM()) return 0xFF;
    return readFromRegister(EEPROM_DATA_ADDRESS);
}

/* Writes value to the eeprom register at address register_address.
 * user eeprom address space : [0x00 - 0x2A]
 * configuration eeprom address space : [0x30 - 0x37] */
void Melopero_RV3028::writeEEPROMRegister(uint8_t registerAddress, uint8_t value){
    writeToRegister(EEPROM_ADDRESS_ADDRESS, registerAddress);
    writeToRegister(EEPROM_DATA_ADDRESS, value);

    // write to a register in eeprom = 0x00 then 0x21
    writeToRegister(EEPROM_COMMAND_ADDRESS, 0x00);
    writeToRegister(EEPROM_COMMAND_ADDRESS, 0x21);
    waitforEEPROM();
}
#endif
