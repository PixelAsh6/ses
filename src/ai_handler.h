#pragma once
#include <Arduino.h>

// Send query to Groq (llama-3.1-8b-instant, free tier).
// If query contains weather keywords, fetches current weather from wttr.in (no key needed).
// Returns the AI text response, or an error string.
String aiQuery(const String& query);
