#pragma once
#include "SDL.h"
#include <SDL_image.h>

// Размер очереди кадров, максимальный
constexpr int QUEUE_SIZE = 5;
// Порт по умолчанию для приема пакетов
constexpr int PORT = 57956;
// Размер окна
constexpr int W_WIDTH = 640;
constexpr int W_HEIGHT = 480;

// Ограничение частоты обновления окна
constexpr int FPS = 60;

// Размер пакета
constexpr size_t PACKET_SIZE = 1307;

// Размер данных кадра в пакете
constexpr size_t PAYLOAD_SIZE = 1300;

// Начальный размер буфера
constexpr size_t BUFFER_SIZE = 10000;

bool sockInit(void);
int createUDPSocket(int port);
bool initSDL(SDL_Window*& window, SDL_Renderer*& renderer, int width, int height);
void cleanup(SDL_Window* window, SDL_Renderer* renderer);
void render(SDL_Renderer* renderer, int frameWidth, int frameHeight);
void receive(int sock);