#include "SimpleMJPEGPlayer.h"

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <map>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include "SDL.h"
#include "SDL_image.h"
#include "SDL_ttf.h"

// Мьютекс для синхронизации работы с очередью
std::mutex queueMutex;
// Очередь для записи и получения кадров
std::queue<FrameData> frameQueue;


//std::map<int, FrameData> rawFrameBuffer;
//std::condition_variable cv;
// 
// Флаг работы программы для потока получения фрагментов
std::atomic_bool running;

// Для статистики
std::atomic<int> totalPackets = 0; // Всего получено фрагментов 
std::atomic<int> goodPackets = 0;  // Всего получено валидных фрагментов 
std::atomic<int> badPackets = 0;   // Всего получено невалидных фрагментов 
std::atomic<int> goodFrames = 0;   // Всего получено кадров

// Инициализация winsock
bool sockInit(void) {
    #ifdef _WIN32
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        return result == 0;
    #else
        return true;
    #endif
}

// Создание сокета для приема кадров
int createUDPSocket(int port) {

    // Выбор протокола UDP
    int sockfd = static_cast<int>(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (sockfd < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return -1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    int bindResult = bind(sockfd, (sockaddr*)&addr, sizeof(addr));

    if (bindResult < 0) {
        std::cerr << "Error binding socket\n";
        #ifdef _WIN32
        std::cerr << "WSA Error: " << WSAGetLastError() << std::endl;
        closesocket(sockfd);
        WSACleanup();
        #else
        close(sockfd);
        #endif
        return -1;
    }

    return sockfd;
}

// Инициализация SDL и создание окна
bool initSDL(SDL_Window*& window, SDL_Renderer*& renderer, int width, int height) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }

    window = SDL_CreateWindow("MJPEG Stream", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_SHOWN + SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError() << "\n";
        return false;
    }

    return true;
}

// Освобождение ресурсов
void cleanup(SDL_Window* window, SDL_Renderer* renderer) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    #ifdef _WIN32
        WSACleanup();
    #endif
}

// Функция рендера текста в окне SDL
void renderText(SDL_Color color, const char *text, TTF_Font *font, SDL_Renderer* renderer, SDL_Rect *textRect) {
    SDL_Surface* textSurface =
        TTF_RenderText_Solid(font, text, color);
    if (textSurface == nullptr) {
        std::cerr << "Text render error: " << TTF_GetError() << std::endl;
        return;
    }

    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);

    SDL_RenderCopy(renderer, textTexture, NULL, textRect);
    //SDL_RenderPresent(renderer);

    SDL_DestroyTexture(textTexture);
}

// Загрузка jpg текстуры в SDL
void loadTexture(SDL_Texture*& dest, SDL_Renderer* renderer, FrameData* data) {
    char* dataPtr = data->data;
    void* buffer = reinterpret_cast<void*>(dataPtr);
    SDL_RWops* rw = SDL_RWFromMem(buffer, data->dataSize);
    if (!rw) {
        std::cerr << "Failed to create SDL_RWops from memory" << std::endl;
        return;
    }

    dest = IMG_LoadTextureTyped_RW(renderer, rw, 1, "JPG");

    free(reinterpret_cast<void*>(dataPtr));
}

// Функция обработки рендера и событий
void render(SDL_Renderer* renderer, int frameWidth, int frameHeight) {
    // Инициализация SDL_Image для работы с jpg кадрами
    int flag = IMG_Init(IMG_INIT_JPG);
    if (flag != IMG_INIT_JPG) {
        return;
    }

    // Инициализация SDL для работы с шрифтами
    flag = TTF_Init();
    if (flag != 0) {
        return;
    }

    // Подгрузка шрифта
    TTF_Font* Sans = TTF_OpenFont("FreeSans.ttf", 24);

    if (Sans == NULL) {
        return;
    }

    // Флаг прерывания для выхода из программы
    bool interrupt = false;
    SDL_Texture* activeTexture = nullptr;
    uint64_t workTime = SDL_GetTicks();
    uint64_t prevWorkTime = workTime;

    while (!interrupt) {

        SDL_Event event;
        // Обработка событий, нажатия клавиш и тд
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                interrupt = true;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    interrupt = true;
                }
                break;
            default:
                break;
            }
        }

        if (interrupt) {
            break;
        }

        workTime = SDL_GetTicks64();
        uint64_t delta = workTime - prevWorkTime;
        // Ограничение fps интерфейса
        if (delta > 1000 / FPS) {

            // Блокировка мьютекса для проверки очереди
            std::lock_guard<std::mutex> lock(queueMutex);
            if (!frameQueue.empty()) {
                // Освобождение предыдущей текстуры
                if (activeTexture != nullptr) {
                    SDL_DestroyTexture(activeTexture);
                }
                // Получение кадра из очереди и загрузка в sdl
                FrameData data = std::move(frameQueue.front());
                frameQueue.pop();
                loadTexture(activeTexture, renderer, &data);
            }

            SDL_RenderClear(renderer);

            // Отрисовка текстуры
            if (activeTexture != nullptr) {
                SDL_RenderCopy(renderer, activeTexture, NULL, NULL);
            }

            SDL_Color whiteColor = { 255, 255, 255, 255 };

            // Рендер текста для статистики
            std::string text = std::to_string(totalPackets) + "/" +
                std::to_string(badPackets) + "/" +
                std::to_string(goodPackets) + "/" +
                std::to_string(goodFrames);

            int text_w = 0;
            int text_h = 0;
            TTF_SizeText(Sans, text.c_str(), &text_w, &text_h);
            SDL_Rect Message_rect = { 0, 0, text_w, text_h };

            renderText(whiteColor, text.c_str(), Sans, renderer, &Message_rect);

            text = "UI FPS: " + std::to_string(1000 / delta);
            TTF_SizeText(Sans, text.c_str(), &text_w, &text_h);
            Message_rect = { 0, text_h + 5, text_w, text_h };
            renderText(whiteColor, text.c_str(), Sans, renderer, &Message_rect);

            SDL_RenderPresent(renderer);

            prevWorkTime = workTime;
        }
    }
    // Освобождение текстуры
    if (activeTexture != nullptr) {
        SDL_DestroyTexture(activeTexture);
    }

    // Освобождение ресурсов библиотек
    IMG_Quit();
    TTF_CloseFont(Sans);
    TTF_Quit();
}

// Функция приема пакетов
void receive(int sock) {

    // Выделяем буфер для записи кадра
    char* buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
    if (buffer == nullptr) {
        return;
    }

    int bufferIndex = 0;
    int currentFrame = -1;
    int bufferSize = static_cast<int>(BUFFER_SIZE);
    int totalFragments = 0;

    // Выделяем массив для записи фрагмента
    char packet[PACKET_SIZE];
    while (running) {
        sockaddr_in clientAddr;
        #ifdef _WIN32
            typedef int socklen_t;
        #endif
        socklen_t clientAddrSize = sizeof(clientAddr);

        int bytesReceived = recvfrom(sock, packet, PACKET_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrSize);
        if (bytesReceived < 0) {

        #ifdef _WIN32
            std::cerr << "recvfrom failed: " << WSAGetLastError() << std::endl;
        #else
            perror("recvfrom failed");
        #endif
            return;
        }

        totalPackets++;
        // Если размер полученного udp пакета не совпадает с стандартым происходит пропуск
        if (bytesReceived != PACKET_SIZE) {
            badPackets++;
            continue;
        }

        // Получение заголовков фрагмента кадра
        // Размер полезной нагрузки в пакете:
        uint16_t payloadSize = reinterpret_cast<unsigned char&>(packet[0]) << 8 | reinterpret_cast<unsigned char&>(packet[1]);
        // Номер кадра:
        uint16_t frameNum = reinterpret_cast<unsigned char&>(packet[2]) << 8 | reinterpret_cast<unsigned char&>(packet[3]);
        // Номер фрагмента - пакета кадра:
        uint16_t packetNum = reinterpret_cast<unsigned char&>(packet[4]) << 8 | reinterpret_cast<unsigned char&>(packet[5]);
        // Флаг последнего фрагмента кадра 
        bool isLastPacket = packet[6] == 0 ? false : true;

        // Если происходит получение первого фрагмента, то происходит сброс буфера с прошлым кадром
        if (packetNum == 0) {
            free(buffer);
            buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
            if (buffer == nullptr) {
                return;
            }

            currentFrame = -1;
            bufferSize = static_cast<int>(BUFFER_SIZE);

            // Сбрасываем буфер для нового кадра
            bufferIndex = 0;
            totalFragments = 0;
            currentFrame = frameNum;
        }
        // Отбрасываем пакет если номер кадра не соответствует текущему
        if (currentFrame != frameNum) {
            badPackets++;
            continue;
        }

        // Проверка размера буфера чтобы все данные поместились
        size_t requiredSize = (packetNum * PAYLOAD_SIZE) + payloadSize;
        if (bufferSize < requiredSize) {
            char* newBuffer = reinterpret_cast<char*>(realloc(buffer, requiredSize));
            if (newBuffer == nullptr) {
                free(buffer);
                return;
            }
            buffer = newBuffer;
            bufferSize = static_cast<int>(requiredSize);

        }
        
        // Смещение для корректной записи фрагментов в общий буфер:
        void* offset = reinterpret_cast<void*>(buffer + (packetNum * PAYLOAD_SIZE));
        if (offset == nullptr) {
            free(buffer);
            return;
        }
        
        // Запись фрагмента в буфер кадра
        memcpy(offset, (packet + 7), payloadSize);
        totalFragments++;

        bufferIndex += payloadSize;

        goodPackets++;

        if (!isLastPacket) {
            continue;
        }

        // Проверка что все фрагменты успешно получены, если нет, то сброс
        if (totalFragments != packetNum + 1) {
            free(buffer);
            buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
            if (buffer == nullptr) {
                return;
            }
            currentFrame = -1;
            bufferSize = static_cast<int>(BUFFER_SIZE);

            // Сбрасываем буфер для нового кадра
            bufferIndex = 0;
            totalFragments = 0;
            continue;
        }

        // Проверка маркера JPEG в конце буфера
        char f = buffer[bufferIndex - 2];
        char s = buffer[bufferIndex - 1];
        char first = static_cast<char>(0xFF);
        char second = static_cast<char>(0xD9);

        if (f != first && s != second) {
            continue;
        }

        // Обновление счетчика успешно принятых кадров:
        goodFrames++;

        // Блокируем мьютекс для синхронизации работы с очередью:
        // Он будет автоматически раблокирован после уничтожения lock_guard
        std::lock_guard<std::mutex> lock(queueMutex);

        // Добавление в очередь

        // Если очередь переполнена, то происходит удаление самого старого кадра - в начале очереди
        if (frameQueue.size() > QUEUE_SIZE) {
            FrameData data = std::move(frameQueue.front());
            frameQueue.pop();
            // Очищаем память
            void* ptr = reinterpret_cast<void*>(data.data);
            free(ptr);
        }

        // Помещаем кадр в конец очереди
        FrameData data = { buffer, bufferSize };
        frameQueue.push(data);

        // Сброс буфера
        buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
        if (buffer == nullptr) {
            return;
        }

        currentFrame = -1;
        bufferSize = static_cast<int>(BUFFER_SIZE);
        bufferIndex = 0;
        totalFragments = 0;
    }
    free(buffer);
}

int main(int argc, char* argv[]) {
    if (!sockInit()) {
        std::cerr << "Failed to initialize Winsock\n";
        return -1;
    }
 
    int sock = createUDPSocket(PORT);
    if (sock < 0) {
        return -1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!initSDL(window, renderer, W_WIDTH, W_HEIGHT)) {
        #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
        #else
        close(sock);
        #endif

        return -1;
    }

    std::cout << "Initialization successful. Starting to receive and display frames.\n";

    running = true;

    // Создание потока для приема пакетов
    std::thread receiveThread(receive, sock);
    // Запуск рендера в основном потоке
    render(renderer, W_WIDTH, W_HEIGHT);

    // Завершаем работу:
    running = false;
    int status = 0;

    #ifdef _WIN32
    status = shutdown(sock, SD_BOTH);
    if (status == 0) { status = closesocket(sock); }
    #else
    status = shutdown(sock, SHUT_RDWR);
    if (status == 0) { status = close(sock); }
    #endif

    receiveThread.join();
    cleanup(window, renderer);
    return 0;
}