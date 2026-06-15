
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

#pragma once

#include <stdint.h>
#include <memory>
#include <type_traits>

#include <BleCompositeHID.h>
#include <BLEHostConfiguration.h>

#include "../gamepadConfig.hpp"

//------------------------------------------------------------------

enum class GamepadButton : uint8_t;

class GamepadDevice;
class XboxGamepadDevice;
//class DualsenseGamepadDevice;

//------------------------------------------------------------------

namespace detail
{
	enum class XboxGamepadDeviceType : uint8_t
	{
		One_S,
		Series_X
	};
}

template<detail::XboxGamepadDeviceType Cfg>
class XboxDeviceAdapter;


// Адаптер для XboxGamepadDevice с конфигом XboxOneSControllerDeviceConfiguration
using XboxOneSDeviceAdapter = XboxDeviceAdapter<detail::XboxGamepadDeviceType::One_S>;
// Адаптер для XboxGamepadDevice с конфигом XboxSeriesXControllerDeviceConfiguration
using XboxSeriesXDeviceAdapter = XboxDeviceAdapter<detail::XboxGamepadDeviceType::Series_X>;


template <detail::XboxGamepadDeviceType DevType>
struct is_supported_xbox_type : std::false_type {};

template <>
struct is_supported_xbox_type<detail::XboxGamepadDeviceType::One_S> : std::true_type {};

template <>
struct is_supported_xbox_type<detail::XboxGamepadDeviceType::Series_X> : std::true_type {};

//------------------------------------------------------------------

// Универсальный интерфейс (чисто виртуальный)
class IDeviceAdapter
{
public:
	virtual ~IDeviceAdapter() = default;

	virtual void attachDeviceToParent(BleCompositeHID& parent) = 0;
	BLEHostConfiguration getIdealHostConfiguration() const;
	
	virtual void resetButtons() = 0;
	virtual void setButtonState(GamepadButton btn, bool state) = 0;

	virtual void setLeftThumb(int16_t leftX, int16_t leftY) = 0;
	virtual void setRightThumb(int16_t rightX, int16_t rightY) = 0;
	virtual void setLeftTrigger(int16_t l2) = 0;
	virtual void setRightTrigger(int16_t r2) = 0;

	/*virtual void setSlider1(int16_t slider1) = 0;
	virtual void setSlider2(int16_t slider2) = 0;
	virtual void setRudder(int16_t rudder) = 0;
	virtual void setThrottle(int16_t throttle) = 0;
	virtual void setAccelerator(int16_t accelerator) = 0;
	virtual void setBrake(int16_t brake) = 0;
	virtual void setSteering(int16_t steering) = 0;*/

	virtual void sendGamepadReport() = 0;

protected:
	// метод дополняющий getIdealHostConfiguration специфичными для того или иного адаптера настройками
	virtual void getSpecificAdapterConfiguration(BLEHostConfiguration& bleHostConfig) const = 0;
};

//------------------------------------------------------------------

// Адаптер для GamepadDevice
class GamepadDeviceAdapter : public IDeviceAdapter
{
	// Направления Dpad / крестовины
	enum DirectionFlag : uint8_t
	{
		D_CENTERED	= 0,	// None
		D_UP		= 1,
		D_DOWN		= 2,
		D_LEFT		= 4,
		D_RIGHT		= 8
	};

public:
	GamepadDeviceAdapter();

	void attachDeviceToParent(BleCompositeHID& parent) override;

	void resetButtons() override;
	void setButtonState(GamepadButton btn, bool state) override;

	void setLeftThumb(int16_t leftX, int16_t leftY) override;		// Левый стик  (-32768 .. 32767)
	void setRightThumb(int16_t rightX, int16_t rightY) override;	// Правый стик (-32768 .. 32767)
	void setLeftTrigger(int16_t l2) override;						// Левый триггер  L2 (-32768 .. 32767)
	void setRightTrigger(int16_t r2) override;						// Правый триггер R2 (-32768 .. 32767)

	void sendGamepadReport() override;

protected:
	void getSpecificAdapterConfiguration(BLEHostConfiguration& bleHostConfig) const override;

	uint8_t getNormalButtonCode(GamepadButton btn) const;
	uint8_t getSpecialButtonCode(GamepadButton btn) const;
	uint8_t getDirectionFlag(GamepadButton btn) const;

	int8_t directionFlagsToValue() const;

private:
	uint8_t directionFlags;

	std::unique_ptr<GamepadDevice> ptrGamepadDevice;
};

//------------------------------------------------------------------

// Адаптер для XboxGamepadDevice (общий для контроллеров XBox One S и XBox Series X)
template<detail::XboxGamepadDeviceType CfgType>
class XboxDeviceAdapter : public IDeviceAdapter
{
public:
	XboxDeviceAdapter();

	void attachDeviceToParent(BleCompositeHID& parent) override;

	void resetButtons() override;
	void setButtonState(GamepadButton btn, bool state) override;

	void setLeftThumb(int16_t leftX, int16_t leftY) override;		// Левый стик  (-32768 .. 32767)
	void setRightThumb(int16_t rightX, int16_t rightY) override;	// Правый стик (-32768 .. 32767)
	void setLeftTrigger(int16_t l2) override;						// Левый триггер  L2 (0 .. 1023) - тормоз
	void setRightTrigger(int16_t r2) override;						// Правый триггер R2 (0 .. 1023) - газ

	void sendGamepadReport() override;

protected:
	void getSpecificAdapterConfiguration(BLEHostConfiguration& bleHostConfig) const override;

	uint16_t getXBoxButtonCode(GamepadButton btn) const;
	uint8_t getDirectionFlag(GamepadButton btn) const;

private:
	uint8_t directionFlags;

	std::unique_ptr<XboxGamepadDevice> ptrGamepadDevice;
};

//------------------------------------------------------------------