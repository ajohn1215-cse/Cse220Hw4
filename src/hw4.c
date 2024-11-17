#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT_PLAYER1 2201
#define PORT_PLAYER2 2202
#define BUFFER_SIZE 1024

typedef enum {
    EMPTY = 0,
    HIT = 'H',
    MISS = 'M'
} CellState;

typedef struct {
    int **grid;
    int width;
    int height;
} Board;

typedef struct {
    int x;
    int y;
} Coordinate;

typedef struct {
    Coordinate blocks[4];
} Shape;

const Shape base_shapes[] = {
    {{ {0, 0}, {1, 0}, {2, 0}, {3, 0} }}, 
    {{ {0, 0}, {0, 1}, {1, 0}, {1, 1} }}, 
    {{ {0, 0}, {1, 0}, {2, 0}, {2, 1} }}, 
    {{ {0, 0}, {1, 0}, {2, 0}, {2, -1} }},
    {{ {0, 0}, {0, 1}, {1, 1}, {1, 2} }}, 
    {{ {0, 1}, {0, 0}, {1, 1}, {1, 2} }}, 
    {{ {0, 0}, {1, -1}, {1, 0}, {1, 1} }} 
};

Board *create_board(int width, int height) {
    Board *board = malloc(sizeof(Board));
    if (!board) {
        perror("Failed to allocate memory for board struct");
        return NULL;
    }

    board->width = width;
    board->height = height;

    board->grid = malloc(height * sizeof(int *));
    if (!board->grid) {
        perror("Failed to allocate memory for board grid");
        free(board);
        return NULL;
    }

    for (int i = 0; i < height; i++) {
        board->grid[i] = calloc(width, sizeof(int));
        if (!board->grid[i]) {
            perror("Failed to allocate memory for board row");
            for (int j = 0; j < i; j++) {
                free(board->grid[j]);
            }
            free(board->grid);
            free(board);
            return NULL;
        }
    }

    return board;
}

void free_board(Board *board) {
    if (board) {
        for (int i = 0; i < board->height; i++) {
            free(board->grid[i]);
        }
        free(board->grid);
        free(board);
    }
}

void print_board(const Board *board) {
    printf("Current Board State:\n");
    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            printf("%2d ", board->grid[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

Coordinate rotate(Coordinate coord) {
    return (Coordinate){.x = coord.y, .y = -coord.x};
}

void calculate_piece_coordinates(int pieceIndex, int rotationCount, int baseRow, int baseCol, Coordinate pieceCoords[4]) {
    const Shape *selectedShape = &base_shapes[pieceIndex]; 
    for (int index = 0; index < 4; index++) {
        Coordinate currentCoord = selectedShape->blocks[index];

        for (int r = 0; r < rotationCount; r++) {
            currentCoord = rotate(currentCoord);  
        }

        pieceCoords[index].x = baseRow + currentCoord.x;
        pieceCoords[index].y = baseCol + currentCoord.y;
    }
}

int insert_piece_on_board(Board *gameBoard, int shapeIndex, int numRotations, int startRow, int startCol, int pieceId) {
    Coordinate pieceCoords[4];
    calculate_piece_coordinates(shapeIndex, numRotations, startRow, startCol, pieceCoords);  

    for (int idx = 0; idx < 4; idx++) {
        int row = pieceCoords[idx].x;
        int col = pieceCoords[idx].y;

        printf("[Server] Checking block %d of piece %d at (%d, %d)\n", idx, shapeIndex, row, col);

        if (row < 0 || row >= gameBoard->height || col < 0 || col >= gameBoard->width) {
            printf("[Server] Block %d of piece %d is out of bounds at (%d, %d)\n", idx, shapeIndex, row, col);
            return 302;  
        }

        if (gameBoard->grid[row][col] != EMPTY) {
            printf("[Server] Block %d of piece %d overlaps at (%d, %d)\n", idx, shapeIndex, row, col);
            return 303;  
        }
    }

    for (int idx = 0; idx < 4; idx++) {
        int row = pieceCoords[idx].x;
        int col = pieceCoords[idx].y;
        gameBoard->grid[row][col] = pieceId;
        printf("[Server] Placed block %d of piece %d at (%d, %d)\n", idx, shapeIndex, row, col);
    }

    return 0; 
}

int is_within_bounds(int row, int col, Board *gameBoard) {
    return row >= 0 && row < gameBoard->height && col >= 0 && col < gameBoard->width;
}

int is_cell_empty(Board *gameBoard, int row, int col) {
    return gameBoard->grid[row][col] == EMPTY;
}

int check_valid_piece_placement(Board *gameBoard, int shapeIndex, int numRotations, int baseRow, int baseCol) {
    Coordinate pieceCoords[4];
    calculate_piece_coordinates(shapeIndex, numRotations, baseRow, baseCol, pieceCoords);

    for (int blockIdx = 0; blockIdx < 4; blockIdx++) {
        int row = pieceCoords[blockIdx].x;
        int col = pieceCoords[blockIdx].y;

        if (!is_within_bounds(row, col, gameBoard)) {
            printf("Error: Block %d out of bounds at (%d, %d)\n", blockIdx, row, col);
            return 302;  
        }

        if (!is_cell_empty(gameBoard, row, col)) {
            printf("Error: Block %d overlaps at (%d, %d)\n", blockIdx, row, col);
            return 303; 
        }
    }

    return 0;  
}


bool validate_packet_header(const char *packet) {
    return strncmp(packet, "I ", 2) == 0;
}

int count_packet_parameters(const char *packet) {
    int count = 0;
    for (int i = 2; packet[i] != '\0'; i++) {
        if (!isspace(packet[i]) && (i == 2 || isspace(packet[i - 1]))) {
            count++;
        }
    }
    return count;
}

int parse_piece(const char *packet, int offset, int *piece_type, int *rotation, int *ref_row, int *ref_col) {
    return sscanf(packet + offset, "%d %d %d %d", piece_type, rotation, ref_row, ref_col);
}

int validate_piece_parameters(int piece_type, int rotation) {
    if (piece_type < 1 || piece_type > 7) {
        return 300; 
    }
    if (rotation < 1 || rotation > 4) {
        return 301; 
    }
    return 0; 
}

int validate_and_place_pieces(Board *temp_board, const char *packet, int num_pieces, int *lowest_error) {
    int offset = 2; 
    for (int i = 0; i < num_pieces; i++) {
        int piece_type, rotation, ref_row, ref_col;
        if (parse_piece(packet, offset, &piece_type, &rotation, &ref_row, &ref_col) != 4) {
            if (*lowest_error == 0 || *lowest_error > 201) {
                *lowest_error = 201; 
            }
            continue;
        }

        int param_error = validate_piece_parameters(piece_type, rotation);
        if (param_error && (*lowest_error == 0 || *lowest_error > param_error)) {
            *lowest_error = param_error;
        }

        piece_type--; 
        rotation--;   

        int placement_error = check_valid_piece_placement(temp_board, piece_type, rotation, ref_row, ref_col);
        if (placement_error && (*lowest_error == 0 || *lowest_error > placement_error)) {
            *lowest_error = placement_error;
        }

        offset += snprintf(NULL, 0, "%d %d %d %d ", piece_type + 1, rotation + 1, ref_row, ref_col);
    }

    return *lowest_error;
}

void place_pieces_on_board(Board *board, const char *packet, int num_pieces) {
    int offset = 2; 
    for (int i = 0; i < num_pieces; i++) {
        int piece_type, rotation, ref_row, ref_col;
        parse_piece(packet, offset, &piece_type, &rotation, &ref_row, &ref_col);

        piece_type--; 
        rotation--;   

        insert_piece_on_board(board, piece_type, rotation, ref_row, ref_col, i + 1);

        offset += snprintf(NULL, 0, "%d %d %d %d ", piece_type + 1, rotation + 1, ref_row, ref_col);
    }
}

int process_initialization_packet(int connectionFd, Board *gameBoard, const char *initPacket) {
    const int expectedPieces = 5;
    int lowestErrorCode = 0;

    if (!validate_packet_header(initPacket)) {
        send(connectionFd, "E 101", strlen("E 101"), 0); 
        return -1;
    }

    if (count_packet_parameters(initPacket) != expectedPieces * 4) {
        send(connectionFd, "E 201", strlen("E 201"), 0); 
        return -1;
    }

    Board *tempBoard = create_board(gameBoard->width, gameBoard->height);
    if (!tempBoard) {
        perror("Failed to allocate memory for temporary board");
        exit(EXIT_FAILURE);
    }

    validate_and_place_pieces(tempBoard, initPacket, expectedPieces, &lowestErrorCode);

    free_board(tempBoard); 

    if (lowestErrorCode != 0) {
        char errorMessage[BUFFER_SIZE];
        snprintf(errorMessage, sizeof(errorMessage), "E %d", lowestErrorCode);
        send(connectionFd, errorMessage, strlen(errorMessage), 0);
        return -1;
    }

    place_pieces_on_board(gameBoard, initPacket, expectedPieces);

    send(connectionFd, "A", strlen("A"), 0);
    return 0;
}

bool is_ship_sunk(Board *board, int piece_id) {
    int shipFound = 0; 
    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            // Check if a part of the ship exists
            if (board->grid[i][j] == piece_id) {
                shipFound = 1;
                break;  
            }
        }
        if (shipFound) {
            break;  
        }
    }

    return !shipFound;
}

void update_ship_counts(Board *board, int *remaining_ships, int max_ships) {
    *remaining_ships = 0; 

    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            int cell = board->grid[i][j];

            if (cell >= 1 && cell <= max_ships) {
                (*remaining_ships)++;  
            }
        }
    }
}

int get_remaining_ships(Board *board, int maxShips) {
    int remaining_ships = 0;

    for (int i = 0; i < board->height; i++) {
        for (int j = 0; j < board->width; j++) {
            int cell = board->grid[i][j];
      
            if (cell >= 1 && cell <= maxShips) {
                remaining_ships++; 
            }
        }
    }

    return remaining_ships;
}



typedef struct Row {
    char *data;
    struct Row *next;
} Row;

Row *allocate_grid(int width, int height) {
    Row *history = NULL;
    Row *current_row = NULL;

    for (int i = 0; i < height; i++) {
        Row *new_row = malloc(sizeof(Row));
        if (!new_row) {
            return NULL;
        }

        new_row->data = malloc(width * sizeof(char));
        if (!new_row->data) {
            free(new_row);
            return NULL;
        }

        for (int j = 0; j < width; j++) {
            new_row->data[j] = 0;
        }

        new_row->next = NULL;

        if (history == NULL) {
            history = new_row;
        } else {
            current_row->next = new_row;
        }

        current_row = new_row;
    }
    return history;
}

char **initialize_shot_history(int width, int height) {
    return allocate_grid(width, height);
}

void free_shot_history(char **history, int height) {
    if (history) {
        for (int i = 0; i < height; i++) {
            free(history[i]);
        }
        free(history);
    }
}

bool parse_shoot_packet(const char *packet, int *row, int *col) {
    char extra;
    if (sscanf(packet, "S %d %d %c", row, col, &extra) != 2) {
        return false; 
    }
    return true;
}

int validate_shot_coordinates(int row, int col, const Board *board, char **shot_history) {
    if (row < 0 || row >= board->height || col < 0 || col >= board->width) {
        return 400; 
    }
    if (shot_history[row][col] != EMPTY) {
        return 401; 
    }
    return 0; 
}

char process_shot(Board *opponent_board, char **shot_history, int row, int col, int *remaining_ships) {
    int piece_id = opponent_board->grid[row][col];
    char shot_result;

    if (piece_id != EMPTY) {
        shot_result = HIT;
        shot_history[row][col] = HIT;
        opponent_board->grid[row][col] = HIT; 

        if (is_ship_sunk(opponent_board, piece_id)) {
            (*remaining_ships)--; 
        }
    } else {

        shot_result = MISS;
        shot_history[row][col] = MISS;
    }

    return shot_result;
}

int process_shoot_action(int connectionFd, Board *opponentBoard, char **shotHistory, int *remainingShips, int opponentConnectionFd, const char *shootPacket) {
    int targetRow, targetCol;


    if (!parse_shoot_packet(shootPacket, &targetRow, &targetCol)) {
        send(connectionFd, "E 202", strlen("E 202"), 0); 
        return -1;
    }

    int validationErrorCode = validate_shot_coordinates(targetRow, targetCol, opponentBoard, shotHistory);
    if (validationErrorCode) {
        char errorMsg[BUFFER_SIZE];
        snprintf(errorMsg, sizeof(errorMsg), "E %d", validationErrorCode);
        send(connectionFd, errorMsg, strlen(errorMsg), 0);
        return -1;
    }

    char shotOutcome = process_shot(opponentBoard, shotHistory, targetRow, targetCol, remainingShips);

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "R %d %c", *remainingShips, shotOutcome);
    send(connectionFd, response, strlen(response), 0);

    if (*remainingShips == 0) {
        send(opponentConnectionFd, "H 0", strlen("H 0"), 0); 
        recv(opponentConnectionFd, response, BUFFER_SIZE, 0); 
        send(connectionFd, "H 1", strlen("H 1"), 0); 
        return 1; 
    }

    return 0; 
}

void append_shot_entry(char *response, char shot, int row, int col) {

    int len = strlen(response);
    snprintf(response + len, BUFFER_SIZE - len, " %c %d %d", shot, row, col);
}

void construct_query_response(char **shot_history, const Board *opponent_board, int remaining_ships, char *response) {

    snprintf(response, BUFFER_SIZE, "G %d", remaining_ships);

    for (int i = 0; i < opponent_board->height; i++) {
        for (int j = 0; j < opponent_board->width; j++) {
            if (shot_history[i][j] == 'H' || shot_history[i][j] == 'M') {
                append_shot_entry(response, shot_history[i][j], i, j);
            }
        }
    }
}

void handle_query_packet(int conn_fd, char **shot_history, Board *opponent_board) {
    int remaining_ships = get_remaining_ships(opponent_board, 5);
    char response[BUFFER_SIZE] = {0}; 
    construct_query_response(shot_history, opponent_board, remaining_ships, response);

    send(conn_fd, response, strlen(response), 0);
}


void notify_player(int conn_fd, const char *message) {
    if (send(conn_fd, message, strlen(message), 0) == -1) {
        perror("[Server] Failed to notify player");
    }
}

void wait_for_acknowledgment(int conn_fd) {
    char buffer[BUFFER_SIZE];
    if (recv(conn_fd, buffer, BUFFER_SIZE, 0) <= 0) {
        perror("[Server] Failed to receive acknowledgment");
    }
}

void handle_forfeit_packet(int forfeiting_player_fd, int opponent_player_fd) {

    notify_player(forfeiting_player_fd, "H 0");

    wait_for_acknowledgment(forfeiting_player_fd);

    notify_player(opponent_player_fd, "H 1");

    printf("[Server] Forfeit handled. Player %d forfeited, Player %d wins.\n", forfeiting_player_fd, opponent_player_fd);
}

bool wait_for_begin_packet(int conn_fd, int *board_width, int *board_height, int opponent_fd) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(conn_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            perror("[Server] Failed to receive Begin or Forfeit packet");
            exit(EXIT_FAILURE);
        }
        buffer[bytes_received] = '\0';

        if (strncmp(buffer, "B", 1) == 0) {
            char remaining_chars;
            int parsed = sscanf(buffer, "B %d %d%c", board_width, board_height, &remaining_chars);
            if (parsed == 2 && *board_width >= 10 && *board_height >= 10) {
                send(conn_fd, "A", strlen("A"), 0);
                printf("[Server] Valid Begin packet received. Board size: %dx%d\n", *board_width, *board_height);
                return true;
            } else {
                send(conn_fd, "E 200", strlen("E 200"), 0);
                fprintf(stderr, "[Server] Invalid board dimensions or malformed Begin packet\n");
            }
        } else if (strcmp(buffer, "F") == 0 || strcmp(buffer, "F\n") == 0) {
            send(conn_fd, "H 0", strlen("H 0"), 0);
            send(opponent_fd, "H 1", strlen("H 1"), 0);
            printf("[Server] Player forfeited during Begin phase. Game halted.\n");
            exit(EXIT_SUCCESS);
        } else {
            send(conn_fd, "E 100", strlen("E 100"), 0);
            fprintf(stderr, "[Server] Invalid packet type received during Begin phase\n");
        }
    }
}

void wait_for_initialize_packet(int conn_fd, Board *player_board, int opponent_fd) {
    char buffer[BUFFER_SIZE];
    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(conn_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            perror("[Server] Failed to receive Initialize or Forfeit packet");
            exit(EXIT_FAILURE);
        }
        buffer[bytes_received] = '\0';

        if (strcmp(buffer, "F") == 0 || strcmp(buffer, "F\n") == 0) {
            send(conn_fd, "H 0", strlen("H 0"), 0);
            send(opponent_fd, "H 1", strlen("H 1"), 0);
            printf("[Server] Player forfeited during Initialize phase. Game halted.\n");
            exit(EXIT_SUCCESS);
        }

        if (process_initialization_packet(conn_fd, player_board, buffer) == 0) {
            printf("[Server] Player's board initialized successfully.\n");
            print_board(player_board);
            break;
        }
    }
}



bool process_turn(int conn_fd, Board *opponent_board, char **shot_history, int *remaining_ships, int opponent_fd) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    int bytes_received = recv(conn_fd, buffer, BUFFER_SIZE, 0);

    if (bytes_received <= 0) {
        perror("[Server] Failed to receive packet from player");
        return false;
    }

    if (strncmp(buffer, "S ", 2) == 0) {
        int result = process_shoot_action(conn_fd, opponent_board, shot_history, remaining_ships, opponent_fd, buffer);
        if (result == 1) {
            send(conn_fd, "H 1", strlen("H 1"), 0);
            send(opponent_fd, "H 0", strlen("H 0"), 0);
            printf("[Server] Game over! Player wins.\n");
            return false; // End game
        }
    } else if (strcmp(buffer, "Q") == 0 || strcmp(buffer, "Q\n") == 0) {
        handle_query_packet(conn_fd, shot_history, opponent_board);
    } else if (strcmp(buffer, "F") == 0 || strcmp(buffer, "F\n") == 0) {
        send(conn_fd, "H 0", strlen("H 0"), 0);
        send(opponent_fd, "H 1", strlen("H 1"), 0);
        printf("[Server] Player forfeited. Game over.\n");
        return false;
    } else {
        send(conn_fd, "E 102", strlen("E 102"), 0);
        fprintf(stderr, "[Server] Invalid command received during player's turn\n");
    }
    return true;
}

void manage_game_session(int player1ConnectionFd, int player2ConnectionFd) {
    int boardWidth, boardHeight;

    printf("[Server] Awaiting 'Begin' packet from Player 1...\n");
    if (!wait_for_begin_packet(player1ConnectionFd, &boardWidth, &boardHeight, player2ConnectionFd)) return;

    printf("[Server] Awaiting 'Begin' packet from Player 2...\n");
    if (!wait_for_begin_packet(player2ConnectionFd, &boardWidth, &boardHeight, player1ConnectionFd)) return;

    Board *player1Board = create_board(boardWidth, boardHeight);
    Board *player2Board = create_board(boardWidth, boardHeight);

    char **player1ShotHistory = initialize_shot_history(boardWidth, boardHeight);
    char **player2ShotHistory = initialize_shot_history(boardWidth, boardHeight);

    int player1RemainingShips = get_remaining_ships(player2Board, 5); 
    int player2RemainingShips = get_remaining_ships(player1Board, 5); 

    printf("[Server] Awaiting 'Initialize' packet from Player 1...\n");
    wait_for_initialize_packet(player1ConnectionFd, player1Board, player2ConnectionFd);

    printf("[Server] Awaiting 'Initialize' packet from Player 2...\n");
    wait_for_initialize_packet(player2ConnectionFd, player2Board, player1ConnectionFd);

    printf("[Server] Both players have initialized their boards. Game starting...\n");

    bool isGameActive = true;
    while (isGameActive) {
        printf("[Server] Player 1's turn...\n");
        isGameActive = process_turn(player1ConnectionFd, player2Board, player1ShotHistory, &player2RemainingShips, player2ConnectionFd);
        if (!isGameActive) break;

        printf("[Server] Player 2's turn...\n");
        isGameActive = process_turn(player2ConnectionFd, player1Board, player2ShotHistory, &player1RemainingShips, player1ConnectionFd);
    }

    printf("[Server] Game over. Cleaning up resources...\n");

    free_board(player1Board);
    free_board(player2Board);
    free_shot_history(player1ShotHistory, boardHeight);
    free_shot_history(player2ShotHistory, boardHeight);
}



int setup_socket(int port) {
    int listen_fd;
    struct sockaddr_in address;
    int opt = 1;

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("[Server] socket() failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("[Server] setsockopt() failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("[Server] bind() failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, 1) == -1) {
        perror("[Server] listen() failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Server] Listening on port %d...\n", port);
    return listen_fd;
}

int accept_connection(int listen_fd, const char *player_name) {
    int conn_fd;
    struct sockaddr_in client_address;
    socklen_t addrlen = sizeof(client_address);

    if ((conn_fd = accept(listen_fd, (struct sockaddr *)&client_address, &addrlen)) == -1) {
        perror("[Server] accept() failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("[Server] %s connected!\n", player_name);
    return conn_fd;
}

int main() {

    int listen_fd1 = setup_socket(PORT_PLAYER1);
    int listen_fd2 = setup_socket(PORT_PLAYER2);

    int conn_fd1 = accept_connection(listen_fd1, "Player 1");
    int conn_fd2 = accept_connection(listen_fd2, "Player 2");

    manage_game_session(conn_fd1, conn_fd2);

    close(conn_fd1);
    close(conn_fd2);
    close(listen_fd1);
    close(listen_fd2);

    return 0;
}