
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

//GyverLibs
#include <GTimer.h>
#include <Blinker.h>

//#include <Bounce2.h>

#include "BLE/ESP32_HID_gamepad.hpp"
#include "BLE/IDeviceAdapter.hpp"
#include "gamepadConfig.hpp"

//=====================================================================================================================================

// Проверка наличия соединения и обновление режима индикации сообщающего о нем и таймера бездействия
void updateConnectionState();

// Считывание нажатых в данный момент кнопок и установка в ESP32_HID_gamepad
void readAllButtons();

// Считывание нажатых в данный момент кнопок кнопок на геймпадах с дублирующими Turbo-кнопками
// и установка в ESP32_HID_gamepad
void readButtonsWithTurbo();


// Колбэк при срабатывании таймера удержания одной из комбинаций
void onCombinationTimeout();

// Колбэк при срабатывании таймера бездействия
void onInactivityTimeout();


// Подготовка устройства ко сну
void preparingForSleep();

//=====================================================================================================================================

//Таймер удержания комбинации кнопок для выключения/смены режима. 3 СЕКУНДЫ
//GTimerCbT<millis, TypeMS> timer_combination(COMBINATION_HOLD_MS, onCombinationTimeout, GTMode::Overflow);
GTimerT<millis, TypeMS> timer_combination(COMBINATION_HOLD_MS, false, GTMode::Overflow);

//Таймер бездействия для выключения при простое более 5 МИНУТ (2 МИНУТЫ при отсутствии сопряжения)
//GTimerCbT<millis, TypeMS> timer_inactivity(INACTIVITY_DISCONNECTED_MS, onInactivityTimeout, GTMode::Overflow);
GTimerT<millis, TypeMS> timer_inactivity(INACTIVITY_DISCONNECTED_MS, false, GTMode::Overflow);

//Светодиод, отображающий статус сопряжения
Blinker btstatus_led(BTLED_PIN);


// Класс геймпада на основе ESP32
ESP32_HID_gamepad hid_gamepad;

//=====================================================================================================================================

void setup()
{
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

	hid_gamepad.begin(ESP32_HID_gamepad::GamepadType::Generic);
	//hid_gamepad.begin(ESP32_HID_gamepad::GamepadType::XBoxSeriesX);
	//hid_gamepad.setPowerOffCombination(POWEROFF_COMBO.data(), POWEROFF_COMBO.size());
	hid_gamepad.setChangeButtonModeCombination(CHMODE_COMBO.data(), CHMODE_COMBO.size());
	hid_gamepad.setButtonMode(ESP32_HID_gamepad::ButtonMode::Standard);

	btstatus_led.blinkForever(DISCONNECTED_BLINK_MS, DISCONNECTED_BLINK_MS);
	timer_inactivity.start();
	timer_combination.stop();
}

//=====================================================================================================================================

void loop()
{
	// Смена индикации сопряжения или его отсутствия, а также обновление таймера бездействия если требуется
	updateConnectionState();
	
	// Обновление (мигание) светодиода статуса сопряжения
	btstatus_led.tick();

	// Отсчёт таймеров (если активны)


	if (timer_combination.tick())		{ onCombinationTimeout();	return; }
	if (timer_inactivity.tick())		{ onInactivityTimeout();	return; }
	
	// Проверка заряда аккумулятора
	//gamepad_power_manager.tick();

	//------------------------------------------------------------------

	const TypeMS now = millis();
	static TypeMS prevGamepadProcessMs = 0;

	if ( REPORT_INTERVAL_MS <= (now - prevGamepadProcessMs) )
	{
		prevGamepadProcessMs = now;

		// Чтение нажатых кнопок сразу в hid_gamepad
		readAllButtons();
		//readButtonsWithTurbo();

		if (hid_gamepad.isAnyButtonPressed())
		{
			if (hid_gamepad.isConnected())
				timer_inactivity.start();  //Сброс таймера бездействия

			bool isPowerOffCombo = hid_gamepad.isPowerOffCombination();				// Комбинация выключения (перехода сон)
			bool isChangeModeCombo = hid_gamepad.isChangeButtonModeCombination();	// Комбинация для смены режима кнопок (Standard/Turbo/Slow)

			// Если удерживается комбинация кнопок для выключения/смены режима
			if (isPowerOffCombo || isChangeModeCombo)
			{
				if (!timer_combination.running())
				{
					timer_combination.start();
					hid_gamepad.sendAllEmptyReport();
				}
				
				return;
			}
		}

		// Попадаем внутрь когда ранее нажатая комбинация смены комбинации была отпущена - предотвращаем её отправку
		if (timer_combination.running())
		{
			timer_combination.stop();
			hid_gamepad.sendAllEmptyReport();
			return;
		}
		
		// Отправка состояния кнопок и осей - метод сам проверит НАЛИЧИЕ соединения и ИЗМЕНИЛАСЬ ЛИ комбинация нажатых/отпущенных
		hid_gamepad.sendReport();
	}
}

//=====================================================================================================================================

void updateConnectionState()
{
	//Статусы соединения: предыдущее и текущее
	static bool prevConnectionStatus = false;
	static bool currConnectionStatus = false;
	
	currConnectionStatus = hid_gamepad.isConnected();
	if (currConnectionStatus != prevConnectionStatus)
	{
		btstatus_led.stop();

		if ( currConnectionStatus )		btstatus_led.blinkForever(UINT16_MAX);
		else							btstatus_led.blinkForever(DISCONNECTED_BLINK_MS, DISCONNECTED_BLINK_MS);

		prevConnectionStatus = currConnectionStatus;


		TypeMS timerInactivityMS = timer_inactivity.getTime();
		if (currConnectionStatus  &&  timerInactivityMS != INACTIVITY_CONNECTED_MS)
		{
			timer_inactivity.stop();
			timer_inactivity.setTime(INACTIVITY_CONNECTED_MS);
		}
		else if (!currConnectionStatus  &&  timerInactivityMS != INACTIVITY_DISCONNECTED_MS)
		{
			timer_inactivity.stop();
			timer_inactivity.setTime(INACTIVITY_DISCONNECTED_MS);
		}

		if (!timer_inactivity.running())
			timer_inactivity.start();
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
      hid_gamepad.setButtonState(currBtn, isCurrButtonPressed);
    }
  }
  
  // Левый и правый стики и триггеры читаются и устанавливаются отдельно как аналоговые значения
  // НА ДАННЫЙ МОМЕНТ ПОКА НЕ РЕАЛИЗОВАНЫ
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
          if (isCurrButtonPressed != hid_gamepad.isTurboButton(GamepadButton::A))
            hid_gamepad.setTurboButton(GamepadButton::A, isCurrButtonPressed);
          break;
        case GamepadButton::Y:
          if (isCurrButtonPressed != hid_gamepad.isTurboButton(GamepadButton::B))
            hid_gamepad.setTurboButton(GamepadButton::B, isCurrButtonPressed);
          break;
        case GamepadButton::Z:
          if (isCurrButtonPressed != hid_gamepad.isTurboButton(GamepadButton::C))
            hid_gamepad.setTurboButton(GamepadButton::C, isCurrButtonPressed);
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
          isCurrButtonPressed |= hid_gamepad.isTurboButton(currBtn);
      }
      // ================================================== TURBO ==================================================

      hid_gamepad.setButtonState(currBtn, isCurrButtonPressed);
    }
  }

  // Левый и правый стики и триггеры читаются и устанавливаются отдельно как аналоговые значения
  // Но  в данном примере не приводятся
}

//=====================================================================================================================================

void onCombinationTimeout()
{
	//timer_combination.stop();

	// Если таймер удержания кнопок достиг требуемого значения и комбинация соответствует - ВЫКЛЮЧАЕМ
	if ( hid_gamepad.isPowerOffCombination() )
	{
		//gamepad_power_manager.sleep();  // Вызовет колбэк preparingForSleep()
	}
	
	// Если таймер удержания кнопок достиг требуемого значения и комбинация соответствует - МЕНЯЕМ РЕЖИМ
	else if ( hid_gamepad.isChangeButtonModeCombination() )
	{
		// Количество значений
		static constexpr uint8_t modeCount = static_cast<uint8_t>(ESP32_HID_gamepad::ButtonMode::ButtonModeCount);
		uint8_t currMode = static_cast<uint8_t>( hid_gamepad.getButtonMode() );
		uint8_t nextMode = (currMode + 1) % modeCount;

		hid_gamepad.setButtonMode( static_cast<ESP32_HID_gamepad::ButtonMode>(nextMode) );


		bool btLedState = digitalRead(BTLED_PIN);
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
	}
	
	timer_combination.start();
}

void onInactivityTimeout()
{
	//timer_inactivity.stop();

	//preparingForSleep();
	
	timer_inactivity.start();
}

//=====================================================================================================================================

// Подготовка ко сну
void preparingForSleep()
{
	if ( hid_gamepad.isConnected() )
		hid_gamepad.end();

	btstatus_led.stop();

	timer_combination.stop();
	timer_inactivity.stop();
}

//=====================================================================================================================================