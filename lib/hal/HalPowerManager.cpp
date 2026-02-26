#include "HalPowerManager.h"

#include <Logging.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include <cassert>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

void HalPowerManager::begin() {
  // Release GPIO hold from deep sleep so pinMode takes effect
  gpio_hold_dis(static_cast<gpio_num_t>(BAT_GPIO0));

  pinMode(BAT_GPIO0, INPUT);
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  auto wifiMode = WiFi.getMode();
  if (wifiMode != WIFI_MODE_NULL) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }

  // Note: We don't use mutex here to avoid too much overhead,
  // it's not very important if we read a slightly stale value for currentLockMode
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !isLowPower) {
    LOG_DBG("PWR", "Going to low-power mode");
    if (!setCpuFrequencyMhz(LOW_POWER_FREQ)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      return;
    }
    isLowPower = true;

  } else if ((!enabled || mode != None) && isLowPower) {
    LOG_DBG("PWR", "Restoring normal CPU frequency");
    if (!setCpuFrequencyMhz(normalFreq)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      return;
    }
    isLowPower = false;
  }

  // Otherwise, no change needed
}

void HalPowerManager::startDeepSleep(HalGPIO& gpio) const {
  // Ensure that the power button has been released to avoid immediately turning back on if you're holding it
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }

  // Pull down the floating ADC button pins to prevent EMI from coupling
  // into the adjacent power button GPIO and causing spurious wake-ups
  pinMode(InputManager::BUTTON_ADC_PIN_1, INPUT_PULLDOWN);
  pinMode(InputManager::BUTTON_ADC_PIN_2, INPUT_PULLDOWN);
  pinMode(BAT_GPIO0, INPUT_PULLDOWN);

  // Hold all GPIO states through deep sleep so pins don't drift
  gpio_hold_en(static_cast<gpio_num_t>(InputManager::BUTTON_ADC_PIN_1));
  gpio_hold_en(static_cast<gpio_num_t>(InputManager::BUTTON_ADC_PIN_2));
  gpio_hold_en(static_cast<gpio_num_t>(BAT_GPIO0));
  gpio_hold_en(static_cast<gpio_num_t>(InputManager::POWER_BUTTON_PIN));
  gpio_deep_sleep_hold_en();

  // Arm the wakeup trigger *after* the button is released
  esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  // Enter Deep Sleep
  esp_deep_sleep_start();
}

int HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery = BatteryMonitor(BAT_GPIO0);
  return battery.readPercentage();
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_ERR("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
