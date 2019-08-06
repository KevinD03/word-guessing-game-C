#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <signal.h>

#include "socket.h"
#include "gameplay.h"


#ifndef PORT
    #define PORT 53744
#endif
#define MAX_QUEUE 5


void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct game_state *game, struct client **top, int fd);

/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf, char* name);
int check_play(struct client **top, int fd);
void announce_turn(struct game_state *game);
void announce_winner(struct game_state *game, struct client *winner);
/* Move the has_next_turn pointer to the next active client */
void advance_turn(struct game_state *game);
/* The following are helpers */
int find_network_newline(const char *buf, int n);
int check_read(struct game_state *game, int fd, char *buf, int room);
int guess_word(struct game_state *game, int fd, char *guess,  char *username);
int update(struct game_state *game, char *guess);
int check_over(struct game_state *game);
int read_username(char *name, struct game_state *game, int fd);
void remove_new_player(struct client **top, int fd);
void add_new_player(struct client **top, int fd, char *name);
void move_to_game(struct client **new_players, int fd, struct game_state *game, char *name);
int is_over(struct game_state *game);
int valid_guess_char(char guess);
int valid_guess_guessed(struct game_state *game, char guess);


/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;

// Check if a player exists according to where they are placed
int check_play(struct client **top, int fd) {
    struct client *p;
    int exist = 0;
    for (p = *top; p != NULL; p = p->next) {
        if (p->fd == fd) {
            exist = 1;
            break;
        }
    }
    return exist;
}

/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset 
 */
void remove_player(struct game_state *game, struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
    ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Disconnect from %s\n", inet_ntoa((*p)->ipaddr));
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));

        // If remove is called to remove a named player
        // if (strcmp(instruction, "no name player") != 0) { // Not removing un-named player
        if ((*p)->name == NULL){
            char bye_message[MAX_MSG];
            strcpy(bye_message, "Goodbye ");
            strcat(bye_message, (*p)->name);
            strcat(bye_message, "\r\n");
            broadcast(game, bye_message, (*p)->name);
        }

        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
        if (game->has_next_turn != NULL && game->head == NULL) {
            game->has_next_turn = NULL;
        } 
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n", fd);
    }
}

// Removes a new player from new player list, helper for move_to_game
void remove_new_player(struct client **top, int fd) {
    struct client **p;
    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next);
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d from new players\n", fd);
        *p = t;
    }
}

// Add a client with name to the game head, helper for move_to_game
void add_new_player(struct client **top, int fd, char *name) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", name);

    p->fd = fd;
    strcpy(p->name, name);
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}



// Write message to all active players
void broadcast(struct game_state *game, char *outbuf, char* name) {
    struct client *p;
    p = game->head;
    while ( p != NULL) {
        if (strcmp(p->name, name) != 0) { 
            if (dprintf(p->fd, "%s", outbuf)< 0) {
                remove_player(game ,&(game->head), p->fd);
            }
        }
        p = p->next;
    }
}

// Tell all palyer that it is which player's turn to play.
void announce_turn(struct game_state *game) {
    struct client *p;
    p = game->head;
    while (p != NULL) {
        if ((game->has_next_turn)->fd == p->fd) { 
            if (dprintf(p->fd, "Your guess?\r\n") < 0) { 
                remove_player(game, &(game->head), p->fd);
            }
        } else { 
            if (dprintf(p->fd, "It's %s's turn\r\n", (game->has_next_turn)->name) < 0) {
                remove_player(game, &(game->head), p->fd);
            }
        }
        p = p->next;
    }
}

// Announce winner's name to playing players.
void announce_winner(struct game_state *game, struct client *winner) {
    struct client *p;
    p = game->head;
    while (p != NULL) {
        if (strcmp(p->name, winner->name) == 0) { 
            if (dprintf(p->fd, "Game over! You win!\n\n\nLet's start a new game\r\n") < 0) { 
                remove_player(game, &(game->head), p->fd);
            }
        } else {
            if (dprintf(p->fd, "Game over! %s won!\n\n\nLet's start a new game\r\n", winner->name) < 0) { 
                remove_player(game, &(game->head), p->fd);
            }
        }
        p = p->next;
    }
}

// Change the has_next_turn pointer to the next active player
void advance_turn(struct game_state *game) {
    if ((game->has_next_turn)->next != NULL) {
        game->has_next_turn = game->has_next_turn->next;
    } else {
        game->has_next_turn = game->head;
    }
}
// Helper for removing a client from new palyer list to game list
void move_to_game(struct client **new_players, int fd, struct game_state *game, char *name) {
    struct client *p;
    p = *new_players;
    while (p != NULL) {
        if (p->fd == fd) {
            remove_new_player(new_players, fd);
            break;
        }
        p = p->next;
    }
    if (game->head == NULL) {
        add_new_player(&(game->head), fd, name);
        game->has_next_turn = p;
        game->has_next_turn = game->head;
    } else { 
        add_new_player(&(game->head), fd, name);
    }
}

// Helper from lab10 to help read
int find_network_newline(const char *buf, int n) {
    for (int k = 0; k < n; k++) {
        if (buf[k] == '\n') {
            return k + 1;
        }
    }
    return -1;
}

// Helper for guess_word and read_username,checking if there is any erroe for read
int check_read(struct game_state *game, int fd, char *buf, int room) {
    int num_read = read(fd, buf, room);
    int exist;
    if (num_read == 0) { 
        exist = check_play(&(game->head), fd);
        if (exist == 1) {
            remove_player(game, &(game->head), fd);
        }
    } else if (num_read < 0) {
        perror("read");
        exit(1);
    }
    return num_read;
}

//helper to see if input is a valid letter
int valid_guess_char(char guess){
    if (guess =='a' || guess =='b' || guess =='c' || guess =='d' || guess =='e' ||
    guess =='f' || guess =='g' || guess =='h' || guess =='i' || guess =='j' || guess =='k' ||
    guess =='l' || guess =='m' || guess =='n' || guess =='o' || guess =='p' || guess =='q' || 
    guess =='r' || guess =='s' || guess =='t' || guess =='u' ||guess =='v' || guess =='w' ||
    guess =='x' || guess =='y' || guess =='z') {
        return 0;
    }
    return 1;
}

// helper to see if the letter has been guessed
int valid_guess_guessed(struct game_state *game, char guess){
    int result = 0;
    if(game->letters_guessed[(int)guess - 97] == 1){
        result = 1;
    }
    return result;
}

// Read a valid guess from player
int guess_word(struct game_state *game, int fd, char *guess,  char *name) {
    int inbuf = 0;           
    int room = sizeof(guess);  
    char *after = guess;       

    int nbytes;
    if ((nbytes = check_read(game, fd, after, room)) > 0) {
        inbuf += nbytes;
        int where;
        if ((where = find_network_newline(guess, inbuf)) > 0) {
            guess [where - 2] = '\0';
            inbuf -= where;
            memmove(guess, &guess[where], inbuf);
        }
        after = &guess[inbuf];
        room = MAX_BUF - inbuf;
    
        if (where == -1) {
            return 1;
        }
        if (fd != (game->has_next_turn)->fd) {
            int num_read = strlen(after) + 2;
            printf("[%d] Read %d bytes\n", fd, num_read);
            printf("[%d] newline %c\n", fd, after[0]);
            printf("Player %s try to guess out of turn\n", name);
            if (dprintf(fd, "It's not your turn to guess\r\n") < 0) { 
                remove_player(game, &(game->head), fd);
            }
            return 1;
        }
        if (valid_guess_char(guess[0]) == 1) {
            if (dprintf(fd, "Please enter a valid letter\r\n") < 0) { 
                remove_player(game, &(game->head), fd);
            }
            return 1;
        }else if (where != 3){
            if (dprintf(fd, "Please enter a single letter\r\n") < 0) {
                remove_player(game, &(game->head), fd);
            }
            return 1;
        }else if (valid_guess_guessed(game, guess[0]) == 1){
            if (dprintf(fd, "Please enter a letter that is not guessed\r\n") < 0) { 
                remove_player(game, &(game->head), fd);
            }
            return 1;
        }
        return 0;
    }
    return 1;
}

// Update the guessed word
int update(struct game_state *game, char *guess) {
    int correct = 1;
    int current_guess_length = strlen(game->word);
    for (int i = 0; i < current_guess_length; i++) {
        if (game->guess[i] == '-' && game->word[i] == *guess) {
            game->guess[i] = guess[0];
            correct = 0;
            game->guesses_left -= 1;
        }
    }
    game->letters_guessed[guess[0] - 97] = 1;
    return correct;
}

// helper to check if the game is over
int is_over(struct game_state *game){
    if ((strcmp(game->guess ,game->word) == 0) || game->guesses_left == 0 ) {
        return 0;
    }
    return 1;
}

// Check if the game must end due to no guessing chance left
int check_over(struct game_state *game) {
    char game_over_msg[MAX_MSG];
    if (is_over(game) == 0) {
        strcpy(game_over_msg, "The word is ");
        strcat(game_over_msg, game->word);
        strcat(game_over_msg, "\n");
        strcat(game_over_msg, "No guesses left. Game over.\n");
        strcat(game_over_msg, "\n");
        strcat(game_over_msg, "Let's start a new game\r\n");
        broadcast(game, game_over_msg, "all");
        return 1;
    }
    return 0;
}

// Helper for reading name input from stdin
int read_username(char *name, struct game_state *game, int fd) {
    int inbuf = 0;           
    int room = sizeof(name);  
    char *after = name;       

    int nbytes;
    if ((nbytes = check_read(game, fd, after, room)) > 0) {
        inbuf += nbytes;
        int where;
        if ((where = find_network_newline(name, inbuf)) > 0) {
            name [where - 2] = '\0';
            inbuf -= where;
            memmove(name, &name[where], inbuf);

        }
        after = &name[inbuf];
        room = MAX_NAME - inbuf;
        if (where == -1) {
            return 1;
        }
        struct client *player;
        player = game->head;
        while(player != NULL){
            if (strcmp(player->name, name) == 0){
                if (dprintf(fd, "Please enter a not used username") < 0) { // Disconnection
                    remove_player(game, &(game->head), fd);
                }
                return 1;
            }
            player = player->next;
        }
        if (name[0] == '\0' ){
            if (dprintf(fd, "Please enter a valid username") < 0) { // Disconnection
                remove_player(game, &(game->head), fd);
            }
            return 1;
        }
        return 0;
    }
    return 1;
}



int main(int argc, char **argv) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;
    
    if(argc != 2){
        fprintf(stderr,"Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }
    
    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);
    
    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;
    
    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;
    
    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);
    
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;

    while (1) {
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }

        if (FD_ISSET(listenfd, &rset)){
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            // printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if(write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_player(&game, &new_players, clientfd);
            };
        }
        
        // To ignore SIGPIPE
        struct sigaction sa;
        sa.sa_handler = SIG_IGN;
        sa.sa_flags = 0;
        sigemptyset(&sa.sa_mask);
        if(sigaction(SIGPIPE, &sa, NULL) == -1) {
            perror("sigaction");
            exit(1);
        }
        
        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */
        int cur_fd, exist, num_read, valid;
        char win[MAX_MSG];
        char guess[MAX_BUF];
        char game_continue_msg[MAX_MSG];
        win[0] = '\0';
        guess[0] = '\0';
        game_continue_msg[0] = '\0';
        for(cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if(FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player
                for(p = game.head; p != NULL; p = p->next) {
                    if (cur_fd == p->fd) {
                        // TODO - handle input from an active client
                        valid = guess_word(&game, cur_fd, guess, p->name);
                        exist = check_play(&(game.head), cur_fd);
                        char *turn = malloc(MAX_MSG);
                        if (turn < 0) {
                            perror("malloc");
                            exit(1);
                        }
                        if (exist && valid == 0) {
                            num_read = strlen(guess) + 2;
                            printf("[%d] Read %d bytes\n", cur_fd, num_read);
                            printf("[%d] newline %s\n",cur_fd, guess);
                            int correct = update(&game, guess);
                            if (strcmp(game.guess, game.word) == 0) {
                                strcat(win, "The word was ");
                                strcat(win, game.word);
                                strcat(win, "\r\n");
                                broadcast(&game, win, "all");
                                announce_winner(&game, p);
                                init_game(&game, argv[1]);
                                announce_turn(&game);
                                printf("Game over. %s won!\n", p->name);
                                printf("New game\n");
                                printf("It's %s's turn.\n", (game.has_next_turn)->name);
                            } else { 
                                if (correct == 1) {
                                    if (dprintf(cur_fd, "%c not in the word\n", guess[0]) < 0) {
                                        remove_player(&game, &(game.head), cur_fd);
                                    }
                                    game.guesses_left -= 1;
                                    advance_turn(&game);
                                    printf("Letter %c is not in the word\n", guess[0]);
                                }
                                strcat(game_continue_msg, p->name);
                                strcat(game_continue_msg, " guesses: ");
                                strncat(game_continue_msg, guess, 1);
                                strcat(game_continue_msg, "\r\n");
                                broadcast(&game, game_continue_msg, "all");
                                turn = status_message(turn, &game);
                                broadcast(&game, turn, "all");
                                announce_turn(&game);
                                printf("It's %s's turn.\n", (game.has_next_turn)->name);
                                free(turn);
                                if (check_over(&game)) {
                                    init_game(&game, argv[1]);
                                    announce_turn(&game);
                                    printf("It's %s's turn.\n", (game.has_next_turn)->name);
                                }
                            }
                            break;
                        } else { 
                            break;
                        }
                    }
                }
                // Check if new players are entering their names
                for(p = new_players; p != NULL; p = p->next) {
                    if(cur_fd == p->fd) {
                        // TODO - handle input from an new client who has
                        char username[MAX_NAME];
                        username[0] = '\0';
                        char *turn = malloc(MAX_MSG);
                        if (turn < 0) {
                            perror("malloc");
                            exit(1);
                        }
                        valid = read_username(username, &game, cur_fd);
                        exist = check_play(&(new_players), cur_fd);
                        if (exist == 0) { 
                            remove_player(&game, &new_players, cur_fd);
                            break;
                        } else if (exist && valid == 0) { 
                            move_to_game(&new_players, cur_fd, &game, username);
                            num_read = strlen(username) + 2;
                            printf("[%d] Read %d bytes\n", cur_fd, num_read);
                            printf("[%d] newline %s\n", cur_fd, username);
                            char enter_game[MAX_MSG];
                            enter_game[0] = '\0';
                            strcat(enter_game, username);
                            strcat(enter_game, " has joined.\r\n");
                            broadcast(&game, enter_game, "all");
                            printf("%s", enter_game);
                            printf("It's %s's turn.\n", (game.has_next_turn)->name);
                            turn = status_message(turn, &game);
                            if (dprintf(cur_fd, "%s", turn) < 0) {
                                remove_player(&game, &(game.head), cur_fd);
                            }
                            free(turn);
                            announce_turn(&game);
                            break;
                        } else if (exist && valid == 1) { 
                            if (dprintf(cur_fd, "\r\n")< 0) { 
                                remove_player(&game, &new_players, cur_fd);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    return 0;
}