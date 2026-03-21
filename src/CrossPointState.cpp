#include "CrossPointState.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 5;
constexpr char STATE_FILE[] = "/.crosspoint/state.bin";
}  // namespace

CrossPointState CrossPointState::instance;

bool CrossPointState::saveToFile() const {
  FsFile outputFile;
  if (!Storage.openFileForWrite("CPS", STATE_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, openEpubPath);
  serialization::writePod(outputFile, lastSleepImage);
  serialization::writePod(outputFile, readerActivityLoadCount);
  serialization::writePod(outputFile, lastSleepFromReader);
  const uint8_t shuffleSize = sleepShuffleOrder.size();
  serialization::writePod(outputFile, shuffleSize);
  for (uint8_t i = 0; i < shuffleSize; i++) {
    serialization::writePod(outputFile, sleepShuffleOrder[i]);
  }
  serialization::writePod(outputFile, sleepShufflePosition);
  outputFile.close();
  return true;
}

bool CrossPointState::loadFromFile() {
  FsFile inputFile;
  if (!Storage.openFileForRead("CPS", STATE_FILE, inputFile)) {
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  if (version > STATE_FILE_VERSION) {
    LOG_ERR("CPS", "Deserialization failed: Unknown version %u", version);
    inputFile.close();
    return false;
  }

  serialization::readString(inputFile, openEpubPath);
  if (version >= 2) {
    serialization::readPod(inputFile, lastSleepImage);
  } else {
    lastSleepImage = 0;
  }

  if (version >= 3) {
    serialization::readPod(inputFile, readerActivityLoadCount);
  }

  if (version >= 4) {
    serialization::readPod(inputFile, lastSleepFromReader);
  } else {
    lastSleepFromReader = false;
  }

  if (version >= 5) {
    uint8_t shuffleSize;
    serialization::readPod(inputFile, shuffleSize);
    sleepShuffleOrder.resize(shuffleSize);
    for (uint8_t i = 0; i < shuffleSize; i++) {
      serialization::readPod(inputFile, sleepShuffleOrder[i]);
    }
    serialization::readPod(inputFile, sleepShufflePosition);
  } else {
    sleepShuffleOrder.clear();
    sleepShufflePosition = 0;
  }

  inputFile.close();
  return true;
}
