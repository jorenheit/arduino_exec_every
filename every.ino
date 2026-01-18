#include "exec_every.h"
using namespace exec;

void setup() {
  Serial.begin(9600);
}

void loop() {
  auto result = exec_every(1000, []() -> String {
    return "Hello World";
  });

  if (result) Serial.println(*result);
}