#include "nlog.h"

#include <cstdarg>
#include <cstdio>
#include <vector>

namespace pokehome::nlog {
namespace {

Sink g_sink;
Rotator g_rotator;
std::size_t g_written = 0;
unsigned g_ticks = 0;

}  // namespace

void SetSink(Sink sink) { g_sink = std::move(sink); }
void SetRotator(Rotator rotator) { g_rotator = std::move(rotator); }

void SetWrittenBytes(std::size_t bytes) { g_written = bytes; }
std::size_t WrittenBytes() { return g_written; }

void Tick(unsigned frames) { g_ticks += frames; }

double Seconds() { return static_cast<double>(g_ticks) / 60.0; }

std::string Format(Cat cat, double seconds, const std::string& message) {
  char head[32];
  // Uma casa decimal: a duas o arquivo fica ruidoso e a granularidade de frame
  // (16 ms) nem justifica a terceira.
  std::snprintf(head, sizeof(head), "[%8.1f] %s ", seconds,
                cat == Cat::kNav ? "[NAV]" : "[ACT]");
  return std::string(head) + message + "\n";
}

void Emit(Cat cat, const char* fmt, ...) {
  if (!g_sink) return;

  // Duas passadas: a primeira mede, a segunda escreve. Buffer fixo truncaria
  // justamente a linha de erro comprida, que e a que mais importa.
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  const int need = std::vsnprintf(nullptr, 0, fmt, ap);
  va_end(ap);
  std::string body;
  if (need > 0) {
    std::vector<char> buf(static_cast<std::size_t>(need) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, ap2);
    body.assign(buf.data(), static_cast<std::size_t>(need));
  }
  va_end(ap2);

  const std::string line = Format(cat, Seconds(), body);

  // Rotaciona ANTES de escrever, nunca depois: rotacionar depois deixaria o
  // arquivo passar do teto por uma linha, e uma linha pode ser longa.
  if (g_written + line.size() > kMaxBytes) {
    if (g_rotator) g_rotator(kKeepRotated);
    g_written = 0;
  }

  g_sink(line);
  g_written += line.size();
}

}  // namespace pokehome::nlog
