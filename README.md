# ������� ��������� MJPEG ������������� � �������������� SDL 2

��� ������� ������������������ (Linux/Windows) ������������� ������ MJPEG
��������� ���������� SDL 2 ��� ������ � SDL Image ��� ������ � Jpeg �������.

---
## ����������

��������� ������� ������������� ���������� SDL2 � SDL Image.

### Windows
���������� ������� � ��������������� � ����� ������� ��� ����� ���������� SDL2 � SDL Image.  
� ����� "SimpleMJPEGPlayer/CMakeLists.txt" ������� ���� � �����������. 

������ �� ����������:  
[SDL 2](https://github.com/libsdl-org/SDL_image/releases)   
[SDL Image](https://github.com/libsdl-org/SDL_image/releases)  
��������� ������ ��� MSVC: "-devel-version-VC"

### Linux
```bash
sudo apt update
sudo apt install libsdl2-dev libsdl2-image-dev
```
---
## �������������
��������� �������� ����������������� ����� ����� UDP �������� � �����
��������� �� � ������� � ������ ��������� ������� �����.  
���� ��� ������� ����������� � "SimpleMJPEGPlayer.h".

### ��������� ������

<table>
	<tbody>
		<tr>
			<td>����</td>
			<td>0 - 15</td>
			<td>16-23</td>
			<td>24-31</td>
		</tr>
		<tr>
			<td>0-31</td>
			<td>������ �������� ��������</td>
			<td colspan="2">����� �����</td>
		</tr>
		<tr>
			<td>32-63</td>
			<td>����� ������ � �����</td>
			<td>���� ���������� ������ �����</td>
			<td>�������� ��������</td>
		</tr>
		<tr>
			<td>64-...</td>
			<td colspan="3">�������� ��������</td>
		</tr>
	</tbody>
</table>