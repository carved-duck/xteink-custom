#pragma once
#include <iosfwd>
#include <string>
#include <vector>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
  uint8_t lastSleepImage;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  std::vector<uint8_t> sleepShuffleOrder;
  uint8_t sleepShufflePosition = 0;
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
