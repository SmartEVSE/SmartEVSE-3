#if SMARTEVSE_VERSION >= 40
#ifndef Melopero_RV3028_H_INCLUDED
#define Melopero_RV3028_H_INCLUDED

#include "Arduino.h"
#include "Wire.h"

#define RV3028_ADDRESS (uint8_t) 0b1010010

#define SECONDS_REGISTER_ADDRESS 0x00
#define MINUTES_REGISTER_ADDRESS 0x01
#define HOURS_REGISTER_ADDRESS 0x02
#define WEEKDAY_REGISTER_ADDRESS 0x03
#define DATE_REGISTER_ADDRESS 0x04
#define MONTH_REGISTER_ADDRESS 0x05
#define YEAR_REGISTER_ADDRESS 0x06

#define SECONDS_TS_REGISTER_ADDRESS 0x15
#define MINUTES_TS_REGISTER_ADDRESS 0x16
#define HOURS_TS_REGISTER_ADDRESS 0x17
#define DATE_TS_REGISTER_ADDRESS 0x18
#define MONTH_TS_REGISTER_ADDRESS 0x19
#define YEAR_TS_REGISTER_ADDRESS 0x1A

#define UNIX_TIME_ADDRESS 0x1B

#define AMPM_HOUR_FLAG 0b00000010
#define PM_FLAG 0b00100000
#define HOURS_ONLY_FILTER_FOR_ALARM 0b00111111

#define MINUTES_ALARM_REGISTER_ADDRESS 0x07
#define HOURS_ALARM_REGISTER_ADDRESS 0x08
#define WEEKDAY_DATE_ALARM_REGISTER_ADDRESS 0x09
#define DATE_ALARM_MODE_FLAG 0b00100000
#define ENABLE_ALARM_FLAG 0b01111111
#define ALARM_FLAG 0b00000100
#define ALARM_INTERRUPT_FLAG 0b00001000

#define TIMER_ENABLE_FLAG 0b00000100
#define TIMER_INTERRUPT_ENABLE_FLAG 0b00010000
#define TIMER_EVENT_FLAG 0b00001000
#define TIMER_REPEAT_FLAG 0b10000000
#define TIMER_VALUE_0_ADDRESS 0x0A
#define TIMER_VALUE_1_ADDRESS 0x0B

#define STATUS_REGISTER_ADDRESS 0x0E

#define CONTROL1_REGISTER_ADDRESS 0x0F
#define CONTROL2_REGISTER_ADDRESS 0x10

#define USER_RAM1_ADDRESS 0x1F
#define USER_RAM2_ADDRESS 0x20

#define EEPROM_ADDRESS_ADDRESS 0x25
#define EEPROM_DATA_ADDRESS 0x26
#define EEPROM_COMMAND_ADDRESS 0x27


enum TimerClockFrequency : uint8_t {
        Hz4096 = 0x00,
        Hz64 = 0x01,
        Hz1 = 0x02,
        Hz1_60 = 0x03
};

class Melopero_RV3028 {

    // instance attributes/members
    public:
        TwoWire *i2c;
        
    //constructor and device initializer
    public:
        Melopero_RV3028();
        void initI2C(TwoWire &bus = Wire);

    //methods
    public:
        uint8_t getSecond();
        uint8_t getMinute();
        uint8_t getHour();

        uint8_t getWeekday();
        uint8_t getDate();
        uint8_t getMonth();
        uint16_t getYear();

        uint32_t getUnixTime();

        uint8_t getTSSecond();
        uint8_t getTSMinute();
        uint8_t getTSHour();

        uint8_t getTSDate();
        uint8_t getTSMonth();
        uint16_t getTSYear();

        void set24HourMode();
        void set12HourMode();
        bool is12HourMode();
        bool isPM();

        //time is always set as 24 hours time!
        void setTime(uint16_t year, uint8_t month, uint8_t weekday, uint8_t date, uint8_t hour, uint8_t minute, uint8_t second);
        //automatically sets the time
        //void setTime(); TODO: implement

        /*
        The alarm can be set for minutes, hours and date or weekday. Any combination of those can be
        selected.
        */
        bool isDateModeForAlarm();
        void setDateModeForAlarm(bool flag);
        void enableAlarm(uint8_t weekdayOrDate, uint8_t hour, uint8_t minute,
            bool dateAlarm = true, bool hourAlarm = true, bool minuteAlarm = true, bool generateInterrupt = true);
        void disableAlarm();

        void enablePeriodicTimer(uint16_t ticks, TimerClockFrequency freq, bool repeat = true, bool generateInterrupt = true);  
        void disablePeriodicTimer();

        //everySecond: if True the periodic time update triggers every second. If False it triggers every minute.
        void enablePeriodicTimeUpdate(bool everySecond = true, bool generateInterrupt = true);
        void disablePeriodicTimeUpdate();

        void clearInterruptFlags(bool clearTimerFlag=true, bool clearAlarmFlag=true, bool clearPeriodicTimeUpdateFlag=true);

        //TODO:
        //void reset();
        // refactor code to use more andOrRegister instead of a read and a write


    public:
        uint8_t readFromRegister(uint8_t registerAddress);
        void writeToRegister(uint8_t registerAddress, uint8_t value);
        void writeToRegisters(uint8_t startAddress, uint8_t *values, uint8_t length);
        void andOrRegister(uint8_t registerAddress, uint8_t andValue, uint8_t orValue);

        // Sets up the device to read/write from/to the eeprom memory. The automatic refresh function has to be disabled.
        void useEEPROM(bool disableRefresh = true);
        bool waitforEEPROM();
        uint8_t readEEPROMRegister(uint8_t registerAddress);
        void writeEEPROMRegister(uint8_t registerAddress, uint8_t value);

        uint8_t BCDtoDEC(uint8_t bcd);
        uint8_t DECtoBCD(uint8_t dec);

        uint8_t to12HourFormat(uint8_t bcdHours);
        uint8_t to24HourFormat(uint8_t bcdHours);
};

#endif // Melopero_RV3028_H_INCLUDED
#endif
