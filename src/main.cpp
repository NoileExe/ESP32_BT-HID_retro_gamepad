
/*
 * ESP32_BT-HID_retro_gamepad
 * Firmware for a retro gamepad based on ESP32, emulating a Bluetooth HID device.
 * Copyright (C) 2026 Belobragin Anton Igorevich
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

//------------------------------------------------------------------

/*
	Все нажатия кнопок на цифровых пинах срабатывают на логический нуль (LOW)
	В режиме сна на ESP32-C3 с 80МГц ~82 мА / В режиме поиска соединения ~80-82 мА мА / В рабочем режиме 82 мА
	Поэтому пока, без должной настройки и отладки, сон отсутствует
*/

#include <Arduino.h>
#include <algorithm>
#include <esp_task_wdt.h>	// WatchDog от зависаний
#include <esp_sleep.h>		// Для определения причины включения и сна

//GyverLibs
#include <GTimer.h>
#include <Blinker.h>

//#include <Bounce2.h>

#include "BLE/ESP32_HID_gamepad.hpp"
#include "gamepadConfig.hpp"

#if USE_MAX1704X_FUEL_GAGE
	#include <Wire.h> // Для I2C
	#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#endif

//=====================================================================================================================================

// Считывание нажатых в данный момент кнопок и установка в ESP32_HID_gamepad
void readAllButtons();

// Считывание нажатых в данный момент кнопок кнопок на геймпадах с дублирующими Turbo-кнопками
// и установка в ESP32_HID_gamepad
void readButtonsWithTurbo();


// Получить текущий процент заряда батареи в мВ
// (если измеряется с делителя на резисторах внутренним ADC из таблицы напряжений с занным шагом % ИЛИ напрямую от MAX1704x)
uint8_t getBatteryPercent(bool stabilizePercent = true);


// Чтение и отправка осей и кнопок, а также отлов комбинаций для выключения и смены режима
// На вход: время для сравнения с предыдущим чтением/отправкой
void updateGamepadState(TypeMS now);

// Проверка заряда аккумулятора,
// TODO "выключение" устройства при критически низком заряде
// На вход: время для сравнения с предыдущей проверкой заряда
void updatePowerState(TypeMS now);

// Проверка наличия соединения и обновление режима индикации сообщающего о нем и таймера бездействия
// На вход: время для сравнения с предыдущей проверкой соединения
void updateConnectionState(TypeMS now);


// Колбэк при срабатывании таймера удержания одной из комбинаций
void onCombinationTimeout();

// Колбэк при срабатывании таймера бездействия
void onInactivityTimeout();

// Подготовка устройства ко сну
void preparingForSleep();

//=====================================================================================================================================

#if USE_MAX1704X_FUEL_GAGE
	// Объект для измерения заряда аккумулятора при помощи микросхем серии MAX1704x с явным указанием её версии
	SFE_MAX1704X batteryLevel(MAX1704X_MAX17043);
#endif

// Данные сохряняющиеся в RTC-памяти между глубоким сном и работой для плавного изменения процента
// при подключении зарядного устройства. СБРОСЯТСЯ только при нажатии RST/перепрошивке или при исчезновении питания
RTC_DATA_ATTR bool isCharging = false;				// Ведется ли зарядка в данный момент
RTC_DATA_ATTR uint8_t lastStablePercent = 100;		// Последний стабильно измерянный процент заряда батареи
RTC_DATA_ATTR uint16_t lastStableVoltage = 4200;	// Последнее стабильно измерянное напряжения в мВ

//=====================================================================================================================================

//Таймер удержания комбинации кнопок для выключения/смены режима. 3 СЕКУНДЫ
GTimerT<millis, TypeMS> timerCombination(COMBINATION_HOLD_MS, false, GTMode::Overflow);

//Таймер бездействия для выключения при простое более 5 МИНУТ (2 МИНУТЫ при отсутствии сопряжения)
GTimerT<millis, TypeMS> timerInactivity(INACTIVITY_DISCONNECTED_MS, false, GTMode::Overflow);

//Светодиод, отображающий статус сопряжения
Blinker btstatusLed(BTLED_PIN);

//Светодиод, сигнализирующий о низком заряде аккумулятора
Blinker powerLed(PWRLED_PIN);


// Класс геймпада на основе ESP32
// с отложенной инициализацией для корректной первоначальной установки процента заряда
//ESP32_HID_gamepad hidGamepad(GAMEPAD_NAME.data(), GAMEPAD_MANUFACTURER.data(), lastStablePercent);
ESP32_HID_gamepad* hidGamepad = nullptr;

//=====================================================================================================================================

void setup()
{
	// === Настройка пинов ===
	pinMode(PWRLED_PIN, OUTPUT);
	digitalWrite(PWRLED_PIN, LOW);
	pinMode(BTLED_PIN, OUTPUT);
	digitalWrite(BTLED_PIN, LOW);
	
	// Инициализация пинов всех кнопок
	for (uint8_t btn = 0; btn < BUTTONS_VARIANTS; btn++)
	{
		uint8_t pin = getButtonPin(static_cast<GamepadButton>(btn));
		
		if (pin != UINT8_MAX)
			pinMode(pin, INPUT_PULLUP);
	}

	// Ожидаем пока кнопку пробуждения отпустят
	uint8_t startPin = getButtonPin(GamepadButton::START);
	while (digitalRead(startPin) == LOW)
	{
		delay(10);
	}

	// === Настройка измерения напряжения ===
#if USE_MAX1704X_FUEL_GAGE
	Wire.begin(SDA_PIN, SCL_PIN);		// Настройка I2C (обязательно перед SFE_MAX1704X::begin())

	if ( !batteryLevel.begin(Wire) )
	{
		// В случае отсутствия соединения с I2C модулем замораживаем выполнение программы
		// и мигаем светодиодом оповещения о низком заряде
		while (true)
		{
			if ( !powerLed.running() )
				powerLed.blinkForever(500, 250);
			
			powerLed.tick();
			delay(50);
		}
	}

	batteryLevel.wake();			// Сбрасываем флаг сна (требуется если модуль переводили в сон)
	delay(300);
	batteryLevel.quickStart();		// Сбрасываем вычисления (после сна могут быть сильно устаревшие данные). К сожалению, 
									// это снизит точность вычисление против случая когда модуль не усыплялся и работал бы непрерывно
	delay(50);
#else
	pinMode(POWER_PIN, INPUT);
	analogReadResolution(12);
	analogSetPinAttenuation(POWER_PIN, ADC_11db);  // ADC_ATTEN_DB_11 = 3.3 В полный диапазон

	delay(300);
#endif

	// === Настройка геймпада ===
	hidGamepad = new ESP32_HID_gamepad(GAMEPAD_NAME.data(), GAMEPAD_MANUFACTURER.data(), getBatteryPercent());

	hidGamepad->begin(ESP32_HID_gamepad::GamepadType::Generic);
	//hidGamepad->begin(ESP32_HID_gamepad::GamepadType::XBoxSeriesX);
	hidGamepad->setPowerOffCombination(POWEROFF_COMBO.data(), POWEROFF_COMBO.size());
	hidGamepad->setChangeButtonModeCombination(CHMODE_COMBO.data(), CHMODE_COMBO.size());
	hidGamepad->setButtonMode(ESP32_HID_gamepad::ButtonMode::Standard);

	// === Светодиоды и таймеры ===
	btstatusLed.blinkForever(DISCONNECTED_BLINK_MS, DISCONNECTED_BLINK_MS);
	timerCombination.stop();
	timerInactivity.start();

	// === Настройка WatchDog ===
	esp_task_wdt_config_t wdt_config =
	{
		.timeout_ms = WDT_TIMEOUT_MS,
		.idle_core_mask = (1 << portNUM_PROCESSORS) - 1,	// Мониторить все ядра
		.trigger_panic = true								// Перезагрузка при срабатывании
	};
	esp_task_wdt_init(&wdt_config);
	esp_task_wdt_add(NULL);				// Добавляем текущий поток в отслеживание WDT
}

//=====================================================================================================================================

void loop()
{
	const TypeMS now = millis();

	// Проверка заряда акккумулятора
	updatePowerState(now);
	powerLed.tick();			// Обновление (мигание) светодиода низкого заряда
	
	// Смена индикации сопряжения или его отсутствия: постоянное свечение или мигание, -
	// а также обновление таймаута таймера бездействия в зависимости от наличия сопряжения (если требуется)
	updateConnectionState(now);
	btstatusLed.tick();			// Обновление (мигание) светодиода статуса сопряжения

	// Отсчёт таймеров (если активны)
	if (timerCombination.tick())		{ onCombinationTimeout();	return; }
	if (timerInactivity.tick())			{ onInactivityTimeout();	return; }

	//------------------------------------------------------------------
	
	updateGamepadState(now);

	// Сброс следящего таймера (защита от зависаний)
	esp_task_wdt_reset();
}

//=====================================================================================================================================

void updateGamepadState(TypeMS now)
{
	// Последнее время считывания кнопок и отправки отчета
	static TypeMS prevGamepadProcessMs = 0;

	if ( REPORT_INTERVAL_MS <= (now - prevGamepadProcessMs) )
	{
		prevGamepadProcessMs = now;

		// Чтение нажатий сразу в hidGamepad
		readAllButtons();
		//readButtonsWithTurbo();

		if (hidGamepad->isAnyButtonPressed())
		{
			if (hidGamepad->isConnected())
				timerInactivity.start();  //Сброс таймера бездействия

			bool isPowerOffCombo = hidGamepad->isPowerOffCombination();				// Комбинация выключения (перехода сон)
			bool isChangeModeCombo = hidGamepad->isChangeButtonModeCombination();	// Комбинация для смены режима кнопок (Standard/Turbo/Slow)

			// Если удерживается комбинация кнопок для выключения/смены режима
			if (isPowerOffCombo || isChangeModeCombo)
			{
				if (!timerCombination.running())
				{
					timerCombination.start();
					hidGamepad->sendAllEmptyReport();
				}
				
				return;
			}
		}

		// Попадаем внутрь когда ранее нажатая комбинация смены комбинации была отпущена - предотвращаем её отправку
		if (timerCombination.running())
		{
			timerCombination.stop();
			hidGamepad->sendAllEmptyReport();
			return;
		}
		
		// Отправка состояния кнопок и осей - метод сам проверит НАЛИЧИЕ соединения и ИЗМЕНИЛАСЬ ЛИ комбинация нажатых/отпущенных
		hidGamepad->sendReport();
	}
}

void readAllButtons()
{
	// Считываем все данные
	for (uint8_t btn = 0; btn < BUTTONS_VARIANTS; btn++)
	{
		GamepadButton currBtn = static_cast<GamepadButton>(btn);

		uint8_t pin = getButtonPin(currBtn);
		if (pin != UINT8_MAX)
		{
			bool isCurrButtonPressed = digitalRead(pin) == LOW;
			hidGamepad->setButtonState(currBtn, isCurrButtonPressed);
		}
	}
  
	// Левый и правый стики и триггеры читаются и устанавливаются отдельно как аналоговые значения
	// TODO НА ДАННЫЙ МОМЕНТ ПОКА НЕ РЕАЛИЗОВАНЫ
}

void readButtonsWithTurbo()
{
	// ================================================== TURBO ==================================================
	// ТОЛЬКО для геймпадов с дублирующими турбо-кнопками
	// Включение/выключение турбо-функционала для отдельных кнопок
	for (uint8_t btn = 0; btn < BUTTONS_VARIANTS; btn++)
	{
		GamepadButton currBtn = static_cast<GamepadButton>(btn);
		uint8_t pin = getButtonPin(currBtn);
	
		if (pin != UINT8_MAX)
		{
			bool isCurrButtonPressed = digitalRead(pin) == LOW;

			// isTurboButton(GamepadButton::A/B/C) == true - означает что нажата соотв. Turbo-кнопка X/Y/Z
			switch (currBtn)
			{
				case GamepadButton::X:
				if (isCurrButtonPressed != hidGamepad->isTurboButton(GamepadButton::A))
					hidGamepad->setTurboButton(GamepadButton::A, isCurrButtonPressed);
					break;
				case GamepadButton::Y:
				if (isCurrButtonPressed != hidGamepad->isTurboButton(GamepadButton::B))
					hidGamepad->setTurboButton(GamepadButton::B, isCurrButtonPressed);
					break;
				case GamepadButton::Z:
				if (isCurrButtonPressed != hidGamepad->isTurboButton(GamepadButton::C))
					hidGamepad->setTurboButton(GamepadButton::C, isCurrButtonPressed);
					break;
			}
		}
	}
	// ================================================== TURBO ==================================================
  
	// Считываем все данные
	for (uint8_t btn = 0; btn < BUTTONS_VARIANTS; btn++)
	{
		GamepadButton currBtn = static_cast<GamepadButton>(btn);

		// ================================================== TURBO ==================================================
		// Полностью игнорируем нажатие X/Y/Z. isTurboButton == true для A/B/C уже означает нажатие X/Y/Z
		switch (currBtn)
		{
			case GamepadButton::X:
			case GamepadButton::Y:
			case GamepadButton::Z:
				continue;
		}
		// ================================================== TURBO ==================================================

		uint8_t pin = getButtonPin(currBtn);

		if (pin != UINT8_MAX)
		{
			bool isCurrButtonPressed = (digitalRead(pin) == LOW);

			// ================================================== TURBO ==================================================
			switch (currBtn)
			{
				case GamepadButton::A:
				case GamepadButton::B:
				case GamepadButton::C:
					isCurrButtonPressed |= hidGamepad->isTurboButton(currBtn);
			}
			// ================================================== TURBO ==================================================

			hidGamepad->setButtonState(currBtn, isCurrButtonPressed);
		}
	}

	// Левый и правый стики и триггеры читаются и устанавливаются отдельно как аналоговые значения
	// Но  в данном примере не приводятся
}

//=====================================================================================================================================

// Получить текущее напряжение батареи в мВ
// (с делителя на резисторах ИЛИ от MAX1704x)
uint16_t getBatteryVoltage_mV()
{
	uint32_t bat_mV = 0;
	
#if USE_MAX1704X_FUEL_GAGE
	float bat_V = batteryLevel.getVoltage() * 1000.0f;
	bat_mV = static_cast<uint16_t>( std::clamp(bat_V, 0.0f, 5000.0f) );
#else
	uint32_t sum = 0;
	for (uint8_t i = 0; i < BATTERY_READ_COUNT; i++)
		sum += analogReadMilliVolts(POWER_PIN);

	uint32_t raw_mV = sum / BATTERY_READ_COUNT;

	//Отладка измерения напряжения
	#if USE_VOLTAGE_MEASURE_DEBUG
		static bool isSerialStarted = false;
		if ( !isSerialStarted )
		{
			Serial.begin(115200);
			while (!Serial) {  }
			isSerialStarted = true;
		}

		Serial.printf(" %.3f V\n*****\n", raw_mV  / 1000.0f);
	#endif

	raw_mV = (raw_mV * VBAT_CALIBRATION_MV) / RAW_CALIBRATION_MV;
	raw_mV += BATTERY_THRESHOLD_MV;

	bat_mV = static_cast<uint16_t>( std::clamp<uint32_t>(raw_mV, 0, 5000) );
#endif

	return bat_mV;
}

// Стабилизация значения процента заряда батареи с защитой от резких скачков напряжения (напр., если подключили зарядное устройство).
// Также изменяет lastStablePercent, lastStableVoltage и isCharging (RTC-память), помогающие в стабилизации между циклами сна и бодрствования
// Метод предназначен для некоторых граничных случаев:
//		первое включение (прошивка, подача питания),
//		выход из сна (заряжали, не заряжали),
//		завышенный процент при критически низком напряжении,
//		резкий скачок с последнего показания на 80-90% при подключении зарядки,
//		резкая просадка с последнего показания до ~реального процента заряда при отключении зарядки
uint8_t stabilizeBatteryPercent(uint8_t raw_percent, uint16_t bat_mV)
{
	if (!isCharging
		&&	MID_CHARGE_DETECT_MV <= bat_mV
		&&	(lastStableVoltage + CHARGE_RISE_DETECT_MV) < bat_mV)
	{
		isCharging = true;
	}
	else if (isCharging
			&&	(bat_mV + CHARGE_FALL_DETECT_MV) <= lastStableVoltage)
	{
		isCharging = false;
	}
	
	//=================================================================================================================================
	
	static bool isFirstCall = true;					// Первый ли вызов (после DeepSleep/подачи питания)

	// === ПЕРВОЕ ИЗМЕРЕНИЕ ПОСЛЕ ВКЛЮЧЕНИЯ ===
	if (isFirstCall)
	{
		esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

		// Холодный старт (прошивка, RESET или полная разрядка)
		// ИЛИ пробуждение из DeepSleep и зарядили во время сна
		if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED || isCharging)
			lastStablePercent = std::min<uint8_t>(raw_percent, 100);

		// Не заряжали пока спали -> защита от отскока напряжения
		else
			lastStablePercent = std::min(lastStablePercent, raw_percent);
		
		lastStableVoltage = bat_mV;
		isFirstCall = false;
	}

	// === НЕ ДОВЕРЯЕМ ЗАВЫШЕННОМУ ПРОЦЕНТУ ===
	else if (bat_mV < BAT_VOLTAGE_MAP[BATTERY_CRITICAL_PERCENT])
	{
		lastStablePercent = std::min(BATTERY_CRITICAL_PERCENT, raw_percent);
		lastStableVoltage = bat_mV;
	}

	// === РЕЖИМ ЗАРЯДКИ ===
	else if (isCharging  &&  lastStablePercent < raw_percent)
	{
		if (lastStablePercent + PERCENT_JUMP_PER_CYCLE < raw_percent)
			lastStablePercent = std::min<uint8_t>(lastStablePercent + PERCENT_JUMP_PER_CYCLE, 100);
		else
			lastStablePercent = std::min<uint8_t>(raw_percent, 100);

		lastStableVoltage = bat_mV;
	}

	// === РЕЖИМ РАЗРЯДА ===
	else if (!isCharging  &&  raw_percent < lastStablePercent)
	{
		if (raw_percent + PERCENT_JUMP_PER_CYCLE < lastStablePercent)
			lastStablePercent = std::max(lastStablePercent - PERCENT_JUMP_PER_CYCLE, 0);
		else
			lastStablePercent = raw_percent;	// Маленькая разница - доверяем сырому значению

		lastStableVoltage = bat_mV;
	}

	// === В ИНОМ СЛУЧАЕ ЗНАЧЕНИЯ НАПРЯЖЕНИЯ И ПРОЦЕНТА СОХРАНЯЮТСЯ ===
	

	//lastStablePercent = std::clamp<uint8_t>(lastStablePercent, 0, 100);
	
	return lastStablePercent;
}

// Получить текущий процент заряда батареи в мВ
// (если измеряется с делителя на резисторах внутренним ADC из таблицы напряжений с занным шагом % ИЛИ напрямую от MAX1704x)
// На вход: флаг означающий стоит ли применять стабилизацию скачков (true) или нет (false)
uint8_t getBatteryPercent(bool stabilizePercent /*= true*/)
{
	// Напряжение аккумулятора в мВ
	const uint16_t bat_mV = getBatteryVoltage_mV();

#if USE_MAX1704X_FUEL_GAGE
	// Процент заряда аккумулятора
	uint8_t raw_percent = static_cast<uint8_t>( batteryLevel.getSOC() + 0.5f );
#else
	uint8_t raw_percent = 0;

	if (BAT_VOLTAGE_MAP.back() <= bat_mV)			raw_percent = 100;
	else if (bat_mV <= BAT_VOLTAGE_MAP.front())		raw_percent = 0;
	else
	{
		auto it = std::upper_bound(BAT_VOLTAGE_MAP.begin(), BAT_VOLTAGE_MAP.end(), bat_mV);
		raw_percent = std::distance(BAT_VOLTAGE_MAP.begin(), it) - 1;
	}
#endif

	if (stabilizePercent)
		raw_percent = stabilizeBatteryPercent(raw_percent, bat_mV);
	else
	{
		lastStablePercent = raw_percent;
		lastStableVoltage = bat_mV;
	}

	return raw_percent;
}

//=====================================================================================================================================

void indicateCritical()
{
	bool btLedState = digitalRead(BTLED_PIN);		// Последний статус свечения статуса сопряжения
	digitalWrite(BTLED_PIN, LOW);
	
	powerLed.stop();
	
	for (int i = 0; i < 3; ++i)
	{
		powerLed.blink(4, 300, 300);	// мигнуть 4 раза, 300мс вкл, 300мс выкл
		
		while (!powerLed.ready())
		{
			powerLed.tick();
			delay(50);
		}
		
		delay(1000);	// 3400 мс
		esp_task_wdt_reset();
	}

	digitalWrite(BTLED_PIN, btLedState);
}

void indicateWarning()
{
	// Предупреждение: медленное мигание (2000 мс вкл / 500 мс выкл)
	if (!powerLed.running())
		powerLed.blinkForever(2000, 500);
	
	powerLed.tick();
}

void updatePowerState(TypeMS now)
{
	// Последнее время проверки заряда
	static TypeMS prevBatteryProcessMs = 0;

	if ( BATTERY_INTERVAL_MS <= (now - prevBatteryProcessMs) )
	{
		prevBatteryProcessMs = now;

		hidGamepad->setBatteryLevel( getBatteryPercent() );

		// Критический разряд: быстро мигаем и TODO уходим в глубокий сон до зарядки
		// Не даем пользоваться устройством пока батарея не будет заряжена
		while ( lastStableVoltage <= BAT_VOLTAGE_MAP[BATTERY_CRITICAL_PERCENT] )
		{
			indicateCritical();
			// TODO переход в глубокий сон

			hidGamepad->setBatteryLevel( getBatteryPercent() );
		}
		
		
		if ( lastStableVoltage <= BAT_VOLTAGE_MAP[BATTERY_WARNING_PERCENT] )
		{
			if ( !powerLed.running() )		indicateWarning();
		}
		
		// Напряжение в норме – гасим светодиод
		else if (powerLed.running())
			powerLed.stop();
	}
}

//=====================================================================================================================================

void updateConnectionState(TypeMS now)
{
	// Последнее время проверки сопряжения
	static TypeMS prevConnectionProcessMs = 0;
	
	// Статусы соединения: предыдущее и текущее
	static bool prevConnectionStatus = false;
	static bool currConnectionStatus = false;

	if ( CONNECTION_INTERVAL_MS <= (now - prevConnectionProcessMs) )
	{
		currConnectionStatus = hidGamepad->isConnected();
		if (currConnectionStatus != prevConnectionStatus)
		{
			prevConnectionStatus = currConnectionStatus;

			
			btstatusLed.stop();

			if ( currConnectionStatus )		btstatusLed.blinkForever(UINT16_MAX);
			else							btstatusLed.blinkForever(DISCONNECTED_BLINK_MS, DISCONNECTED_BLINK_MS);


			TypeMS timerInactivityMS = timerInactivity.getTime();
			if (currConnectionStatus  &&  timerInactivityMS != INACTIVITY_CONNECTED_MS)
			{
				timerInactivity.stop();
				timerInactivity.setTime(INACTIVITY_CONNECTED_MS);
			}
			else if (!currConnectionStatus  &&  timerInactivityMS != INACTIVITY_DISCONNECTED_MS)
			{
				timerInactivity.stop();
				timerInactivity.setTime(INACTIVITY_DISCONNECTED_MS);
			}

			if (!timerInactivity.running())
				timerInactivity.start();
		}
	}
}

//=====================================================================================================================================

void onCombinationTimeout()
{
	//timerCombination.stop();

	// Если таймер удержания кнопок достиг требуемого значения и комбинация соответствует - ВЫКЛЮЧАЕМ
	if ( hidGamepad->isPowerOffCombination() )
	{
		// TODO переход в глубокий сон
	}
	
	// Если таймер удержания кнопок достиг требуемого значения и комбинация соответствует - МЕНЯЕМ РЕЖИМ
	else if ( hidGamepad->isChangeButtonModeCombination() )
	{
		// Количество значений
		constexpr uint8_t modeCount = static_cast<uint8_t>(ESP32_HID_gamepad::ButtonMode::ButtonModeCount);
		uint8_t currMode = static_cast<uint8_t>( hidGamepad->getButtonMode() );
		uint8_t nextMode = (currMode + 1) % modeCount;

		hidGamepad->setButtonMode( static_cast<ESP32_HID_gamepad::ButtonMode>(nextMode) );


		bool btLedState = digitalRead(BTLED_PIN);		// Последний статус свечения статуса сопряжения
		bool pwrLedState = digitalRead(PWRLED_PIN);		// Последний статус свечения низкого заряда
		digitalWrite(BTLED_PIN, LOW);

		// Сообщаем пользователю о смене моргая светодиодом питания, 
		// БЛОКИРУЯ дальнейшее выполнения кода до окончания цикла индикации
		for (int i = 0; i < 5; i++)
		{
			digitalWrite(PWRLED_PIN, HIGH);
			delay(100);
			digitalWrite(PWRLED_PIN, LOW);
			delay(100);
		}

		digitalWrite(BTLED_PIN, btLedState);
		digitalWrite(PWRLED_PIN, pwrLedState);
	}
	
	timerCombination.start();
}

void onInactivityTimeout()
{
	//timerInactivity.stop();

	// TODO переход в глубокий сон
	//preparingForSleep();
	
	timerInactivity.start();
}

//=====================================================================================================================================

// Подготовка ко сну
void preparingForSleep()
{
	if ( hidGamepad->isConnected() )
		hidGamepad->end();

	btstatusLed.stop();
	powerLed.stop();

	timerCombination.stop();
	timerInactivity.stop();

	#if USE_MAX1704X_FUEL_GAGE
		batteryLevel.sleep();		// Экономия 50-80 мкА
	#endif

	// TODO переход в глубокий сон
}

//=====================================================================================================================================