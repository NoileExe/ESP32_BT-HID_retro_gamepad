
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

//GyverLibs
#include <GTimer.h>
#include <Blinker.h>

//#include <Bounce2.h>

#include "BLE/ESP32_HID_gamepad.hpp"
#include "BLE/IDeviceAdapter.hpp"
#include "gamepadConfig.hpp"

//=====================================================================================================================================

// Считывание нажатых в данный момент кнопок и установка в ESP32_HID_gamepad
void readAllButtons();

// Считывание нажатых в данный момент кнопок кнопок на геймпадах с дублирующими Turbo-кнопками
// и установка в ESP32_HID_gamepad
void readButtonsWithTurbo();


// Проверка заряда аккумулятора,
// TODO вычисление и установка в ESP32_HID_gamepad для отправки процента заряда хосту
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

//Таймер удержания комбинации кнопок для выключения/смены режима. 3 СЕКУНДЫ
//GTimerCbT<millis, TypeMS> timerCombination(COMBINATION_HOLD_MS, onCombinationTimeout, GTMode::Overflow);
GTimerT<millis, TypeMS> timerCombination(COMBINATION_HOLD_MS, false, GTMode::Overflow);

//Таймер бездействия для выключения при простое более 5 МИНУТ (2 МИНУТЫ при отсутствии сопряжения)
//GTimerCbT<millis, TypeMS> timerInactivity(INACTIVITY_DISCONNECTED_MS, onInactivityTimeout, GTMode::Overflow);
GTimerT<millis, TypeMS> timerInactivity(INACTIVITY_DISCONNECTED_MS, false, GTMode::Overflow);

//Светодиод, отображающий статус сопряжения
Blinker btstatusLed(BTLED_PIN);

//Светодиод, сигнализирующий о низком заряде аккумулятора
Blinker powerLed(PWRLED_PIN);


// Класс геймпада на основе ESP32
ESP32_HID_gamepad hidGamepad;

//=====================================================================================================================================

void setup()
{
	pinMode(PWRLED_PIN, OUTPUT);
	digitalWrite(PWRLED_PIN, LOW);
	pinMode(BTLED_PIN, OUTPUT);
	digitalWrite(BTLED_PIN, LOW);

	pinMode(POWER_PIN, INPUT);
	analogReadResolution(12);
	analogSetPinAttenuation(POWER_PIN, ADC_11db);  // ADC_ATTEN_DB_11 = 3.3 В полный диапазон
	
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


	hidGamepad.begin(ESP32_HID_gamepad::GamepadType::Generic);
	//hidGamepad.begin(ESP32_HID_gamepad::GamepadType::XBoxSeriesX);
	hidGamepad.setPowerOffCombination(POWEROFF_COMBO.data(), POWEROFF_COMBO.size());
	hidGamepad.setChangeButtonModeCombination(CHMODE_COMBO.data(), CHMODE_COMBO.size());
	hidGamepad.setButtonMode(ESP32_HID_gamepad::ButtonMode::Standard);

	btstatusLed.blinkForever(DISCONNECTED_BLINK_MS, DISCONNECTED_BLINK_MS);
	timerCombination.stop();
	timerInactivity.start();
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
	
	// Последнее время считывания кнопок и отправки отчета
	static TypeMS prevGamepadProcessMs = 0;

	if ( REPORT_INTERVAL_MS <= (now - prevGamepadProcessMs) )
	{
		prevGamepadProcessMs = now;

		// Чтение нажатий сразу в hidGamepad
		readAllButtons();
		//readButtonsWithTurbo();

		if (hidGamepad.isAnyButtonPressed())
		{
			if (hidGamepad.isConnected())
				timerInactivity.start();  //Сброс таймера бездействия

			bool isPowerOffCombo = hidGamepad.isPowerOffCombination();				// Комбинация выключения (перехода сон)
			bool isChangeModeCombo = hidGamepad.isChangeButtonModeCombination();	// Комбинация для смены режима кнопок (Standard/Turbo/Slow)

			// Если удерживается комбинация кнопок для выключения/смены режима
			if (isPowerOffCombo || isChangeModeCombo)
			{
				if (!timerCombination.running())
				{
					timerCombination.start();
					hidGamepad.sendAllEmptyReport();
				}
				
				return;
			}
		}

		// Попадаем внутрь когда ранее нажатая комбинация смены комбинации была отпущена - предотвращаем её отправку
		if (timerCombination.running())
		{
			timerCombination.stop();
			hidGamepad.sendAllEmptyReport();
			return;
		}
		
		// Отправка состояния кнопок и осей - метод сам проверит НАЛИЧИЕ соединения и ИЗМЕНИЛАСЬ ЛИ комбинация нажатых/отпущенных
		hidGamepad.sendReport();
	}
}

//=====================================================================================================================================

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
	  hidGamepad.setButtonState(currBtn, isCurrButtonPressed);
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
		  if (isCurrButtonPressed != hidGamepad.isTurboButton(GamepadButton::A))
			hidGamepad.setTurboButton(GamepadButton::A, isCurrButtonPressed);
		  break;
		case GamepadButton::Y:
		  if (isCurrButtonPressed != hidGamepad.isTurboButton(GamepadButton::B))
			hidGamepad.setTurboButton(GamepadButton::B, isCurrButtonPressed);
		  break;
		case GamepadButton::Z:
		  if (isCurrButtonPressed != hidGamepad.isTurboButton(GamepadButton::C))
			hidGamepad.setTurboButton(GamepadButton::C, isCurrButtonPressed);
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
		  isCurrButtonPressed |= hidGamepad.isTurboButton(currBtn);
	  }
	  // ================================================== TURBO ==================================================

	  hidGamepad.setButtonState(currBtn, isCurrButtonPressed);
	}
  }

  // Левый и правый стики и триггеры читаются и устанавливаются отдельно как аналоговые значения
  // Но  в данном примере не приводятся
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
			//TODO wdt reset
			delay(50);
		}
		
		delay(1000);
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

uint8_t get_battery_percent(uint16_t voltage_mv)
{
    if (voltage_mv >= BAT_VOLTAGE_MAP.front())  return 100;
    if (voltage_mv <= BAT_VOLTAGE_MAP.back())   return 0;

    for (size_t i = 0; i < BAT_VOLTAGE_MAP.size() - 1; i++)
    {
        // Напряжения верхней и нижней границы текущего интервала
        uint16_t v_high = BAT_VOLTAGE_MAP[i];
        uint16_t v_low  = BAT_VOLTAGE_MAP[i + 1];

        if (voltage_mv >= v_low  &&  voltage_mv <= v_high)
        {
            float percent_high = 100 - i * BAT_PERCENT_STEP;        // процент для верхней границы (больше %)
            float percent_low  = 100 - (i + 1) * BAT_PERCENT_STEP;  // процент для нижней границы (меньше %)

            // Доля, на которую измеренное напряжение находится внутри интервала [0.0, 1.0]
            // (0.0 — если voltage_mv == v_low, 1.0 — если == v_high)
            float ratio = static_cast<float>(voltage_mv - v_low) / static_cast<float>(v_high - v_low);
            
            // SOC (State of Charge) — уровень заряда батареи в процентах
			// C округлением до ближайшего целого в перспективе (добавляем 0.5 перед отбрасыванием дробной части)
            float soc = percent_low + ratio * (percent_high - percent_low) + 0.5f;

			// Ограничиваем диапазон значений и отбрасываем дробную часть
			uint8_t res = static_cast<uint8_t>( std::clamp(soc, 0.0f, 100.0f) );
            return res;
        }
    }

    return 0;
}

uint16_t get_battery_voltage_mV()
{
	// Измерено: при реальных 5100 мВ на источнике питания, analogReadMilliVolts() выдаёт 2526 мВ
	// Коэффициент коррекции = 5100 / 2526
	// Показания при делителе Rверх = 100 кОм и Rниз = 100 кОм
	// Rверх - от Vbat+ к пину измерения, Rниз - от GND к пину измерения
	constexpr uint16_t vbatCalibration_mv = 5100;    // Реальное напряжение на источнике питания в мВ
	constexpr uint16_t rawCalibration_mv = 2526;     // Показание analogReadMilliVolts на пине в мВ
	
	
	uint32_t sum = 0;
	for (uint8_t i = 0; i < BATTERY_READ_COUNT; i++)
	{
		sum += analogReadMilliVolts(POWER_PIN);
	}

	uint32_t raw_mV = sum / BATTERY_READ_COUNT;

	//Отладка измерения напряжения
	/*{
		static bool isSerialStarted = false;
		if ( !isSerialStarted )
		{
			Serial.begin(115200);
			while (!Serial) {  }
			isSerialStarted = true;
		}

		float bat_V = raw_mV / 1000.0f;
		uint8_t percent = get_battery_percent(raw_mV);

		Serial.printf(" raw_mV == %u\n", raw_mV);
		Serial.printf("%.3f V = %u %%\n*****\n", bat_V, percent);
	}*/

	raw_mV = (raw_mV * vbatCalibration_mv) / rawCalibration_mv;

	return static_cast<uint16_t>(raw_mV);
}

void updatePowerState(TypeMS now)
{
	// Последнее время проверки заряда
	static TypeMS prevBatteryProcessMs = 0;

	if ( BATTERY_INTERVAL_MS <= (now - prevBatteryProcessMs) )
	{
		prevBatteryProcessMs = now;
		
		uint16_t bat_mV = get_battery_voltage_mV();
		uint8_t percent = get_battery_percent(bat_mV);

		hidGamepad.setBatteryLevel(percent);

		// Критический разряд: быстро мигаем TODO и уходим в глубокий сон до зарядки
		// TODO Не даем пользоваться устройством пока батарея не будет заряжена
		while (bat_mV <= BATTERY_CRITICAL_LEVEL)
		{
			indicateCritical();
			// TODO переход в глубокий сон
		}
		
		
		if ( (bat_mV + VOLTAGE_ACCURACY_LEVEL) < BATTERY_WARNING_LEVEL )
			indicateWarning();
		
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
		currConnectionStatus = hidGamepad.isConnected();
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
	if ( hidGamepad.isPowerOffCombination() )
	{
		// TODO переход в глубокий сон
	}
	
	// Если таймер удержания кнопок достиг требуемого значения и комбинация соответствует - МЕНЯЕМ РЕЖИМ
	else if ( hidGamepad.isChangeButtonModeCombination() )
	{
		// Количество значений
		constexpr uint8_t modeCount = static_cast<uint8_t>(ESP32_HID_gamepad::ButtonMode::ButtonModeCount);
		uint8_t currMode = static_cast<uint8_t>( hidGamepad.getButtonMode() );
		uint8_t nextMode = (currMode + 1) % modeCount;

		hidGamepad.setButtonMode( static_cast<ESP32_HID_gamepad::ButtonMode>(nextMode) );


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
	if ( hidGamepad.isConnected() )
		hidGamepad.end();

	btstatusLed.stop();
	powerLed.stop();

	timerCombination.stop();
	timerInactivity.stop();

	// TODO переход в глубокий сон
}

//=====================================================================================================================================