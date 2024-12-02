# Простой потоковый MJPEG проигрыватель с использованием SDL 2

Это простой кроссплатформенный (Linux/Windows) проигрыватель потока MJPEG
Программа использует SDL 2 для вывода и SDL Image для работы с Jpeg кадрами.

---
## Требования

Программа требует установленные библиотеки SDL2 и SDL Image.

### Windows
Необходимо скачать и разархивировать в любое удобное вам место библиотеки SDL2 и SDL Image.  
В файле "SimpleMJPEGPlayer/CMakeLists.txt" укажите пути к библиотекам. 

Ссылки на библиотеки:  
[SDL 2](https://github.com/libsdl-org/SDL_image/releases)   
[SDL Image](https://github.com/libsdl-org/SDL_image/releases)  
Загрузите версию для MSVC: "-devel-version-VC"

### Linux
```bash
sudo apt update
sudo apt install libsdl2-dev libsdl2-image-dev
```
---
## Использование
Программа получает фрагментированные кадры через UDP протокол и затем
склеивает их и выводит в случае получения полного кадра.  
Порт для пакетов указывается в "SimpleMJPEGPlayer.h".

### Структура пакета

<table>
	<tbody>
		<tr>
			<td>Биты</td>
			<td>0 - 15</td>
			<td>16-23</td>
			<td>24-31</td>
		</tr>
		<tr>
			<td>0-31</td>
			<td>Размер полезной нагрузки</td>
			<td colspan="2">Номер кадра</td>
		</tr>
		<tr>
			<td>32-63</td>
			<td>Номер пакета в кадре</td>
			<td>Флаг последнего пакета кадра</td>
			<td>Полезная нагрузка</td>
		</tr>
		<tr>
			<td>64-...</td>
			<td colspan="3">Полезная нагрузка</td>
		</tr>
	</tbody>
</table>