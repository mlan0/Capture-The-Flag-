#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <string.h>

#include "libSocket/server.h"

#include "allegro5/allegro.h"
#include "allegro5/allegro_image.h"
#include "allegro5/allegro_primitives.h"
#include "allegro5/allegro_font.h"

#define TILEMAP_HEIGHT 1184
#define TILEMAP_WIDTH 960
#define TILE_SIZE 16
#define FPS 60
#define N_FLAGS 2
#define MAX_PLAYERS 4
#define N_BULLETS 10
#define BULLET_SPEED 10
#define BULLET_LIFE 30
#define HIT 1
#define MAX_LIFE 5
#define BOUND 8

#define SKIN_H 21
#define SKIN_W 16

#define WALKABLE 1

enum TileMapObjects {WALL = 2, TREE, CAVE, FLAG_1, FLAG_2, BASE_1, BASE_2};
enum Teams{TEAM_1, TEAM_2};
enum Events {MOVED, FIRED, CATCHED_ENEMY_FLAG, CATCHED_TEAM_FLAG, DROPPED_ENEMY_FLAG, DROPPED_TEAM_FLAG, WON, RESPAWNED, BOOTED, DISCONNECTED};

typedef struct{
	short int x;
	short int y;
} Point;

typedef struct{
	float x;
	float y;
} FloatPoint;

typedef struct{
	float vx;
	float vy;
} Vector;

typedef struct{
	char id;
	char team;
	char live;
	char kills;
	char deaths;
	char skin;
	char name[13];
	
	bool has_team_flag;
	bool has_enemy_flag;
	
	Vector speed;
	FloatPoint mouse;
	Point pos;
} Player;

typedef struct{
	char *map;
	char rows;
	char cols;
} Map;

typedef struct{
	char r;
	char c;
	char type;
} TileObject;

typedef struct{
	char player_id;
	char live;
	Vector speed;
	FloatPoint pos;
} Bullet;

typedef struct{
	char src_r;
	char src_c;
	bool has_catched;
	TileObject tile;
} Flag;

typedef struct{
	short int spawn_r;
	short int spawn_c;
} Team;

typedef struct{
	Player players[MAX_PLAYERS];
	Flag flags[N_FLAGS];
	Bullet bullets[N_BULLETS*MAX_PLAYERS];
	char victorious_team;
	bool ends;
} ServerPacket;

typedef struct{
	char id;
	char name[13];
	char skin;
	Point pos;
	Point mouse;
	Vector speed;
	char event;
} ClientPacket;

typedef struct{
	char id;
	Point pos;
} BootPacket;

typedef struct{
	ALLEGRO_MUTEX *mutex;
	ServerPacket *game;
	char client_id;
	bool ready;
} ThreadData;

void loadTileMapMatrix();
void showTileMapMatrix();
char getTileContentByPosition(int x, int y);
void setTileContentByPosition(int x, int y, char tile_id);
char getTileContent(char r, char c);
void setTileContent(char r, char c, char tile_id);

bool isWalkable(int x, int y);
bool isPassable(int x, int y);

void spawnPlayer(Player *player);
void respawnPlayer(ThreadData *data, Player *player);

bool catchEnemyFlag(ThreadData *data, Player *player);
bool catchTeamFlag(ThreadData *data, Player *player);
bool dropEnemyFlag(ThreadData *data, Player *player);
bool dropTeamFlag(ThreadData *data, Player *player);

void initFlags();
void initPlayers();
void initBullets();

void processBullets();
void fireBullet(ThreadData *data, Player *player);

void *rcvFromClient(ALLEGRO_THREAD *thr, void *arg);
void receiveFromClient(ThreadData *data, ClientPacket *packet, char id);


Map *map = NULL;
Flag *flags = NULL;
Bullet *bullets = NULL;
Player *players = NULL;
ServerPacket *game = NULL;
Team teams[N_FLAGS];
ALLEGRO_THREAD *threads[10];

int main(){
	register int i;
	serverInit(10);
	
	game = (ServerPacket *) malloc(sizeof(ServerPacket));
	flags = game->flags;
	bullets = game->bullets;
	players = game->players;
	game->ends = false;
	
	
	loadTileMapMatrix();
	showTileMapMatrix();
	
	initFlags();
	initPlayers();
	initBullets();
	
	ALLEGRO_TIMER *timer = NULL;
	ALLEGRO_EVENT_QUEUE *queue = NULL;
	ALLEGRO_DISPLAY *display = NULL;
	
	al_init();
	al_install_keyboard();
	
	display = al_create_display(800, 640);
	al_set_window_title(display, "SERVER RUNNING");
	al_clear_to_color(al_map_rgb(0, 0, 0));
	
	timer = al_create_timer(1.0 / FPS);
	queue = al_create_event_queue();
	al_register_event_source(queue, al_get_timer_event_source(timer));
	al_register_event_source(queue, al_get_keyboard_event_source());

	ThreadData data;
	data.game = (ServerPacket *) malloc(sizeof(ServerPacket));
	*(data.game) = *game;
	data.mutex = al_create_mutex();
	
	ALLEGRO_EVENT event;
	bool run = true;
	al_start_timer(timer);
	
	while(run){
		al_wait_for_event(queue, &event);
		al_lock_mutex(data.mutex);
		*(game) = *(data.game);
		if(game->ends){
			broadcast(game, sizeof(ServerPacket));
			initFlags();
			initPlayers();
			initBullets();
			game->ends = false;
		}
		if (event.type == ALLEGRO_EVENT_DISPLAY_CLOSE) run = 0;
		if (event.type == ALLEGRO_EVENT_KEY_DOWN){
			if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE) run = 0;
		}
		if(event.type == ALLEGRO_EVENT_TIMER){
			processBullets();
		}
		*(data.game) = *(game);
		broadcast(game, sizeof(ServerPacket));
		al_unlock_mutex(data.mutex);
		int id = acceptConnection();
		if(id != NO_CONNECTION){
			al_lock_mutex(data.mutex);
			Player *player = players+id;
			BootPacket *boot = (BootPacket *) malloc(sizeof(BootPacket));
			boot->id = id;
			spawnPlayer(player);
			boot->pos.x = player->pos.x;
			boot->pos.y = player->pos.y;
			sendMsgToClient(boot, sizeof(BootPacket), id);
			sendMsgToClient(game, sizeof(ServerPacket), id);
			*(data.game) = *(game);
			data.client_id = id;
			threads[id] = al_create_thread(rcvFromClient, &data);
			al_start_thread(threads[id]);
			al_unlock_mutex(data.mutex);
			printf("Player %d spawned in %d  %d.\n", boot->id, boot->pos.x, boot->pos.y);
		}
	}
	serverReset();
	for(i = 0; i < MAX_PLAYERS; i++) if(threads[i] != NULL) al_destroy_thread(threads[i]);
	al_destroy_mutex(data.mutex);
	al_destroy_event_queue(queue);
	al_destroy_timer(timer);
	return 0;
}
void loadTileMapMatrix(){
	register int i, t = 0, ro = 0, ca = 0;
	char r, c;
	map = (Map *) malloc(sizeof(Map));
	map->rows = TILEMAP_HEIGHT/TILE_SIZE;
	map->cols = TILEMAP_WIDTH/TILE_SIZE;
	map->map = (char *) malloc(sizeof(char)*map->rows*map->cols);
	FILE *file = fopen("mapServer.txt", "r");
	char b;
	short int n_spawns;
	for(i = 0; i < N_FLAGS; i++){
		fscanf(file, " %hi %hi", &teams[i].spawn_r, &teams[i].spawn_c);
	}
	for(i = 0; i < map->rows*map->cols; i++){
		b = fgetc(file);
		if(b == '\n') b = fgetc(file);
		if(isalpha(b)){
			b-='A'-10;
		}else if(isdigit(b)){
			b-='0';
		}
		r = i/map->cols;
		c = i%map->cols;
		if(b == FLAG_1){
			flags[0].src_r = r;
			flags[0].src_c = c;
			flags[0].tile.type = b;
		}else if(b == FLAG_2){
			flags[1].src_r = r;
			flags[1].src_c = c;
			flags[1].tile.type = b;
		}
		*(map->map+i) = b;
	}
	fclose(file);
}
void showTileMapMatrix(){
	register int i, j;
	for(i = 0; i < map->rows; i++){
		printf("%d", getTileContent(i, 0));
		for(j = 1; j < map->cols; j++){
			printf(" %d", getTileContent(i, j));
		}
		printf("\n");
	}
}
void initBullets(){
	register int i;
	for(i = 0; i < N_BULLETS*MAX_PLAYERS; i++){
		bullets[i].live = 0;
	}
}
void initPlayers(){
	register int i;
	for(i = 0; i < MAX_PLAYERS/2; i++){
		players[i].id = i;
		players[i].team = TEAM_1;
		
	}
	for(; i < MAX_PLAYERS; i++){
		players[i].id = i;
		players[i].team = TEAM_2;
	}
	players[i].live = 0;
	players[i].pos.x = 0;
	players[i].pos.y = 0;
	players[i].has_team_flag = false;
	players[i].has_enemy_flag = false;
}
void initFlags(){
	flags[0].tile.r = flags[0].src_r;
	flags[0].tile.c = flags[0].src_c;
	flags[0].has_catched = 0;
	flags[1].tile.r = flags[1].src_r;
	flags[1].tile.c = flags[1].src_c;
	flags[0].has_catched = 0;
}
char getTileContentByPosition(int x, int y){
	char r = y/TILE_SIZE, c = x/TILE_SIZE;
	return *(map->map+(r*map->cols + c));
}
char getTileContent(char r, char c){
	return *(map->map+(r*map->cols + c));
}
void setTileContentByPosition(int x, int y, char tile_id){
	char r = y/TILE_SIZE, c = x/TILE_SIZE;
	*(map->map+(r*map->cols+c)) = tile_id;
}
void setTileContent(char r, char c, char tile_id){
	*(map->map+(r*map->cols+c)) = tile_id;
}
void *rcvFromClient(ALLEGRO_THREAD *thr, void *arg){
	ThreadData *data  = (ThreadData*) arg;
	al_lock_mutex(data->mutex);
	int id = data->client_id;
	al_unlock_mutex(data->mutex);
	struct msg_ret_t retorno;
	ClientPacket *request = (ClientPacket *) malloc(sizeof(ClientPacket));
	bool run = 1;
	while(run && !al_get_thread_should_stop(thr)){
		retorno = recvMsgByClient(request, id);
		if(retorno.status == MESSAGE_OK){
			id = retorno.client_id;
			al_lock_mutex(data->mutex);
			if(request->event == DISCONNECTED) run = false;
			receiveFromClient(data, request, id);
			al_unlock_mutex(data->mutex);
		}
	}
	return NULL;
}
void receiveFromClient(ThreadData *data, ClientPacket *packet, char id){
	char event = packet->event;
	Player *player = data->game->players+id;
	if(event == DISCONNECTED){
		printf("Player %d disconnected!\n", id);
		dropEnemyFlag(data, player);
		dropTeamFlag(data, player);
		player->live = 0;
		disconnectClient(id);
		return;
	}
	strcpy(player->name, packet->name);
	player->skin = packet->skin;
	player->speed.vx = packet->speed.vx;
	player->speed.vy = packet->speed.vy;
	player->pos.x = packet->pos.x;
	player->pos.y = packet->pos.y;
	player->mouse.x = packet->mouse.x;
	player->mouse.y = packet->mouse.y;
	switch(event){
		case MOVED:{
			//printf("Player %d moved to %hi %hi!\n", player->id, player->pos.x, player->pos.y);
			break;
		}
		case FIRED:{
			fireBullet(data, player);
			printf("Player %d fired!\n", player->id);
			break;
		}
		case CATCHED_ENEMY_FLAG:{
			printf("Player %d catch enemy flag!\n", player->id);
			if(catchEnemyFlag(data, player)){
				player->has_enemy_flag = 1;
				printf("Player %d catched enemy flag!\n", player->id);
			}
			break;
		}
		case CATCHED_TEAM_FLAG:{
			if(catchTeamFlag(data, player)){
				player->has_team_flag = 1;
				printf("Player %d catched team flag!\n", player->id);
			}
			break;
		}
		case DROPPED_ENEMY_FLAG:{
			if(dropEnemyFlag(data, player)){
				player->has_enemy_flag = 0;
				printf("Player %d dropped enemy flag!\n", player->id);
			}
			break;
		}
		case DROPPED_TEAM_FLAG:{
			if(dropTeamFlag(data, player)){
				player->has_team_flag = 0;
				printf("Player %d dropped team flag!\n", player->id);
			}
			break;
		}
		case RESPAWNED:{
			respawnPlayer(data, player);
			break;
		}
		case WON:{
			if(player->has_team_flag && player->has_enemy_flag && getTileContentByPosition(player->pos.x, player->pos.y) == BASE_1+player->team){
				data->game->victorious_team = player->team;
				data->game->ends = true;
			}
		}
	}
}
void processBullets(){
	register int i, j;
	for(i = 0; i < N_BULLETS*MAX_PLAYERS; i++){
		if(bullets[i].live){
			bullets[i].pos.x += bullets[i].speed.vx;
			bullets[i].pos.y += bullets[i].speed.vy;
			if(!isPassable(bullets[i].pos.x, bullets[i].pos.y)){
				bullets[i].live = 0;
			}else{
				bullets[i].live--;
			}
			if(bullets[i].player_id < MAX_PLAYERS/2){
				for(j = MAX_PLAYERS/2; j < MAX_PLAYERS; j++){
					if(players[j].live){
						if((bullets[i].pos.x >= players[j].pos.x-SKIN_W/3 && bullets[i].pos.x <= players[j].pos.x+SKIN_W/3) && (bullets[i].pos.y >= players[j].pos.y && bullets[i].pos.y <= players[j].pos.y+SKIN_H/2)){
							players[j].live -= HIT;
							bullets[i].live = 0;
							printf("Player %d fired by %d!\n", j, bullets[i].player_id);
							if(players[j].live == 0){
								players[bullets[i].player_id].kills++;
								players[j].deaths++;
							}
						}
					}
				}
			}else{
				for(j = 0; j < MAX_PLAYERS/2; j++){
					if(players[j].live){
						if((bullets[i].pos.x >= players[j].pos.x-SKIN_W/3 && bullets[i].pos.x <= players[j].pos.x+SKIN_W/3) && (bullets[i].pos.y >= players[j].pos.y && bullets[i].pos.y <= players[j].pos.y+SKIN_H/2)){
							players[j].live -= HIT;
							bullets[i].live = 0;
							printf("Player %d fired by %d!\n", j, bullets[i].player_id);
							if(players[j].live == 0){
								players[bullets[i].player_id].kills++;
								players[j].deaths++;
							}
						}
					}
				}
			}
		}
	}
}
void fireBullet(ThreadData *data, Player *player){
	register int i;
	float module = sqrt(player->mouse.x*player->mouse.x+player->mouse.y*player->mouse.y);
	float vx = (float)BULLET_SPEED / (module/player->mouse.x);
	float vy = (float)BULLET_SPEED / (module/player->mouse.y);
	Bullet *bullet = NULL;
	for(i = 0; i < N_BULLETS*MAX_PLAYERS; i++){
		bullet = data->game->bullets+i;
		if(!bullet->live){
			bullet->player_id = player->id;
			bullet->pos.x = player->pos.x;
			bullet->pos.y = player->pos.y;
			bullet->speed.vx = vx;
			bullet->speed.vy = vy;
			bullet->live = BULLET_LIFE;
			break;
		}
	}
}
bool isPassable(int x, int y){
	char r = y/TILE_SIZE, c = x/TILE_SIZE;
	if(x < 0 || y < 0 || x > TILEMAP_WIDTH || y > TILEMAP_HEIGHT) return 0;
	if(getTileContent(r, c) == WALL) return 0;
	if(getTileContent(r, c) == TREE) return 0;
	return 1;
}
void spawnPlayer(Player *player){
	player->pos.x = teams[player->team].spawn_c*TILE_SIZE;
	player->pos.y = teams[player->team].spawn_r*TILE_SIZE;
	player->live = MAX_LIFE;
	player->has_team_flag = 0;
	player->has_enemy_flag = 0;
	player->kills = player->deaths = 0;
}
void respawnPlayer(ThreadData *data, Player *player){
	player->pos.x = teams[player->team].spawn_c*TILE_SIZE;
	player->pos.y = teams[player->team].spawn_r*TILE_SIZE;
	player->has_team_flag = 0;
	player->has_enemy_flag = 0;
	player->live = MAX_LIFE;
}
bool catchEnemyFlag(ThreadData *data, Player *player){
	char enemy_flag = FLAG_1 + !player->team;
	if(getTileContentByPosition(player->pos.x, player->pos.y) == enemy_flag){
		data->game->flags[!player->team].has_catched = 1;
		setTileContentByPosition(player->pos.x, player->pos.y, WALKABLE);
		return 1;
	}
	return 0;
}
bool catchTeamFlag(ThreadData *data, Player *player){
	char team_flag = FLAG_1 + player->team;
	if(getTileContentByPosition(player->pos.x, player->pos.y) == team_flag){
		data->game->flags[player->team].has_catched = 1;
		setTileContentByPosition(player->pos.x, player->pos.y, WALKABLE);
		return 1;
	}
	return 0;
}
bool dropEnemyFlag(ThreadData *data, Player *player){
	char r = player->pos.y/TILE_SIZE, c = player->pos.x/TILE_SIZE;
	register int i = 0;
	if(player->has_enemy_flag){
		if(getTileContent(r, c) != WALKABLE){
			while(true){
				if(c-i >= 0 && getTileContent(r, c-i) == WALKABLE){
					c-=i;
					break;
				}else if(r-i >= 0 && getTileContent(r-i, c) == WALKABLE){
					r-=i;
					break;
				}else if(c+i < map->cols && getTileContent(r, c+i) == WALKABLE){
					c+=i;
					break;
				}else if(r+i < map->rows && getTileContent(r+i, c) == WALKABLE){
					r+=i;
					break;
				}
				i++;
			}
		}
		char enemy_flag = FLAG_1 + !player->team;
		data->game->flags[!player->team].tile.r = r;
		data->game->flags[!player->team].tile.c = c;
		data->game->flags[!player->team].has_catched = 0;
		setTileContent(r, c, enemy_flag);
		return 1;
	}
	return 0;
}
bool dropTeamFlag(ThreadData *data, Player *player){
	char r = player->pos.y/TILE_SIZE, c = player->pos.x/TILE_SIZE;
	register int i = 0;
	if(player->has_team_flag){
		if(getTileContent(r, c) != WALKABLE){
			while(true){
				if(c-i >= 0 && getTileContent(r, c-i) == WALKABLE){
					c-=i;
					break;
				}else if(r-i >= 0 && getTileContent(r-i, c) == WALKABLE){
					r-=i;
					break;
				}else if(c+i < map->cols && getTileContent(r, c+i) == WALKABLE){
					c+=i;
					break;
				}else if(r+i < map->rows && getTileContent(r+i, c) == WALKABLE){
					r+=i;
					break;
				}
				i++;
			}
		}
		char team_flag = FLAG_1 + player->team;
		data->game->flags[player->team].tile.r = r;
		data->game->flags[player->team].tile.c = c;
		data->game->flags[player->team].has_catched = 0;
		setTileContent(r, c, team_flag);
		return 1;
	}
	return 0;
}
