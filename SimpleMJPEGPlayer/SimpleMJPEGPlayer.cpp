#include "SimpleMJPEGPlayer.h"

#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>

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
#include <atomic>

struct FrameData
{
    char* data;
    int dataSize;
};

std::mutex queueMutex;
std::queue<FrameData> frameQueue;
std::atomic_bool running;
std::condition_variable cv;

bool sockInit(void) {
    #ifdef _WIN32
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        return result == 0;
    #else
        return true;
    #endif
}

int createUDPSocket(int port) {
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

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Error binding socket\n";
        #ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
        #else
        close(sockfd);
        #endif
        return -1;
    }

    return sockfd;
}

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

void cleanup(SDL_Window* window, SDL_Renderer* renderer) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    #ifdef _WIN32
        WSACleanup();
    #endif
}

void render(SDL_Renderer* renderer, int frameWidth, int frameHeight) {
    int flag = IMG_Init(IMG_INIT_JPG);
    if (flag != IMG_INIT_JPG) {
        return;
    }

    flag = TTF_Init();
    if (flag != 0) {
        return;
    }

    TTF_Font* Sans = TTF_OpenFont("FreeSans.ttf", 24);

    if (Sans == NULL) {
        return;
    }

    bool interrupt = true;

    while (interrupt) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                interrupt = false;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    interrupt = false;
                }
                break;
            default:
                break;
            }
        }
        if (!interrupt) {
            break;
        }

        
        SDL_Color White = { 255, 255, 255, 255 };
        SDL_Surface* textSurface =
            TTF_RenderText_Solid(Sans, "put your text here", White);
        if (textSurface == nullptr) {
            std::cerr << "Text render error: " << TTF_GetError() << std::endl;
            return;
        }
        SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
        SDL_FreeSurface(textSurface);

        SDL_Rect Message_rect = { 0, 0, textSurface->w, textSurface->h };

        SDL_RenderCopy(renderer, textTexture, NULL, &Message_rect);
        SDL_RenderPresent(renderer);

        // Don't forget to free your surface and texture
        SDL_DestroyTexture(textTexture);

        std::lock_guard<std::mutex> lock(queueMutex);
        if (frameQueue.empty()) {
            // Ограничение частоты кадров
            SDL_Delay(1000 / FPS); // fps lock
            continue;
        }

        FrameData data = std::move(frameQueue.front());
        frameQueue.pop();
        char* dataPtr = data.data;
        void* buffer = reinterpret_cast<void*>(dataPtr);

        SDL_RWops* rw = SDL_RWFromMem(buffer, data.dataSize);
        if (!rw) {
            std::cerr << "Failed to create SDL_RWops from memory" << std::endl;
            continue;
        }
        
        SDL_Surface* frame = IMG_Load_RW(rw, 0);

        if (frame == NULL) {
            SDL_FreeRW(rw);
            free(reinterpret_cast<void*>(dataPtr));
            std::cerr << "SDL_Init Error:" << SDL_GetError() << std::endl;
            SDL_Log(SDL_GetError());
            std::cerr << "Failed to load image from memory" << std::endl;

            continue;
        }

        SDL_Texture* frameTexture = SDL_CreateTextureFromSurface(renderer, frame);
        SDL_FreeSurface(frame);
        free(reinterpret_cast<void*>(dataPtr));
        if (!frameTexture) {
            std::cerr << "Failed to create texture from surface" << std::endl;
            continue;
        }

        // Очистка рендера и отрисовка текстуры
        //SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, frameTexture, NULL, NULL);
        SDL_RenderPresent(renderer);

        // Освобождение текстуры после отрисовки
        SDL_DestroyTexture(frameTexture);

    }
    IMG_Quit();
    TTF_CloseFont(Sans);
    TTF_Quit();
}

void receive(int sock) {
    char* buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
    if (buffer == nullptr) {
        return;
    }

    int bufferIndex = 0;
    int currentFrame = -1;
    int bufferSize = static_cast<int>(BUFFER_SIZE);
    int totalFragments = 0;
    char packet[PACKET_SIZE];
    while (running) {
        sockaddr_in clientAddr;
        #ifdef _WIN32
            typedef int socklen_t;
        #endif
        socklen_t  clientAddrSize = sizeof(clientAddr);

        int bytesReceived = recvfrom(sock, packet, PACKET_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrSize);
        if (bytesReceived < 0) {

        #ifdef _WIN32
            std::cerr << "recvfrom failed: " << WSAGetLastError() << std::endl;
        #else
            perror("recvfrom failed");
        #endif
            return;
        }

        if (bytesReceived != PACKET_SIZE) {
            continue;
        }
        uint16_t payloadSize = reinterpret_cast<unsigned char&>(packet[0]) << 8 | reinterpret_cast<unsigned char&>(packet[1]);
        uint16_t frameNum = reinterpret_cast<unsigned char&>(packet[2]) << 8 | reinterpret_cast<unsigned char&>(packet[3]);
        uint16_t packetNum = reinterpret_cast<unsigned char&>(packet[4]) << 8 | reinterpret_cast<unsigned char&>(packet[5]);
        bool isLastPacket = packet[6] == 0 ? false : true;
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
        if (currentFrame != frameNum) {
            continue;
        }
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
        void* offset = reinterpret_cast<void*>(buffer + (packetNum * PAYLOAD_SIZE));
        if (offset == nullptr) {
            free(buffer);
            return;
        }
        memcpy(offset, (packet + 7), payloadSize);
        totalFragments++;

        bufferIndex += payloadSize;
        if (!isLastPacket) {
            continue;
        }

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

        char f = buffer[bufferIndex - 2];
        char s = buffer[bufferIndex - 1];
        char first = static_cast<char>(0xFF);
        char second = static_cast<char>(0xD9);

        if (f != first && s != second) {
            continue;
        }

        std::lock_guard<std::mutex> lock(queueMutex);
        // Добавление в очередь
        if (frameQueue.size() > QUEUE_SIZE) {
            FrameData data = std::move(frameQueue.front());
            frameQueue.pop();
            void* ptr = reinterpret_cast<void*>(data.data);
            free(ptr);
        }

        FrameData data = { buffer, bufferSize };
        frameQueue.push(data);

        buffer = reinterpret_cast<char*>(malloc(BUFFER_SIZE));
        if (buffer == nullptr) {
            return;
        }

        currentFrame = -1;
        bufferSize = static_cast<int>(BUFFER_SIZE);

        // Сбрасываем буфер для нового кадра
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

    std::thread receiveThread(receive, sock);
    render(renderer, W_WIDTH, W_HEIGHT);
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