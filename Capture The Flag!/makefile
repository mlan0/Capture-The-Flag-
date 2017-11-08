serverFile=server
clientFile=client
menuFile=menu
serverName=SERVER
clientName=CLIENT
menuName=MENU

all: compServer compClient

server: compServer runServer

client: compClient runClient

menu: compMenu runMenu

compClient:
	gcc -std=c99 -o $(clientName) $(clientFile).c libSocket/client.c -lallegro -lallegro_image -lallegro_primitives -lallegro_font -lallegro_ttf -lallegro_audio -lallegro_acodec -lm

compServer:
	gcc -std=c99 -o $(serverName) $(serverFile).c libSocket/server.c -lallegro -lallegro_image -lallegro_primitives -lallegro_font -lallegro_ttf  -lm

compMenu:
	gcc -std=c99 -o $(menuName) $(menuFile).c libSocket/client.c -lallegro -lallegro_image -lallegro_primitives -lallegro_font -lallegro_ttf -lallegro_audio -lallegro_acodec -lm

runClient:
	./$(clientName)

runServer:
	./$(serverName)
	
runMenu:
	./$(menuName)
