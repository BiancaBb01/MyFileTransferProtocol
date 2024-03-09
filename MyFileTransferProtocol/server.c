#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#define PORT 2024
#define MAX_LEN 1024
int errno;
int client; //descriptorul clientului
int auth_state = 0; //0 - neautentificat, 1 - autentificat
char* client_current_dir = NULL;

//functii utilitare
void read_from_client(char* buffer);
void write_to_client(char* buffer);
char* get_password_hash(char* username);

//functii de prelucrare a comenzilor
void handler(int client_desc);
void parse_command(char* buffer);
void cmd_help(char* buffer);
void cmd_login(char* buffer);
void cmd_logout(char* buffer);
void cmd_upload(char* buffer);
void cmd_list(char* buffer);
void cmd_cwd(char* buffer);
void cmd_cd(char* buffer);
void cmd_newdir(char* buffer);
void cmd_newfile(char* buffer);
void cmd_rename(char* buffer);
void cmd_delete(char* buffer);

int main(int argc, char *argv[]) {
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	bzero(&server_address, sizeof(server_address));
	bzero(&client_address, sizeof(client_address));

	printf("Serverul porneste...\n");

	int sockd; //descriptor socket
	if((sockd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("Eroare la crearea socket-ului");
		return errno;
	}

	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htonl(INADDR_ANY);
	server_address.sin_port = htons(PORT);

	if(bind(sockd, (struct sockaddr *) &server_address, sizeof(struct sockaddr)) == -1) {
		perror("Eroare la atasarea socket-ului (bind)");
		return errno;
	}

	//initializarea spatiului de stocare
	struct stat s;
	if(stat("storage", &s) == -1 || !S_ISDIR(s.st_mode)) {
		if(mkdir("storage", 0700) == -1) {
			perror("Eroare la initializarea spatiului de stocare");
			return errno;
		}
	}
	printf("Spatiu de stocare initializat.\n");

	//ascultam pe socket
	if(listen(sockd, 1) == -1){
		perror ("Eroare la listen");
		return errno;
	}
	printf("Server pornit. Asteptam clienti la portul %d...\n", PORT);

	while(1) {
		unsigned length = sizeof(client_address);
		fflush(stdout);

		//acceptam conexiunea -- apel blocant pana la aparitia unei conexiuni
		client = accept(sockd, (struct sockaddr*) &client_address, &length);
		if(client < 0) {
			perror ("Eroare la acceptarea conexiunii unui client");
			continue; //continuam sa ascultam
		}

		//conexiune acceptata, asiguram concurenta
		int pid;
		if((pid = fork()) == -1) {
			perror("Eroare la fork.");
			close(client);
			continue;
		} else if(pid > 0) { //parinte
			close(client); //inchidem socketul copilului
			continue;
		} else if(pid == 0) { //copil
			close(sockd); //inchidem socketul parintelui
			printf("Client acceptat. ID proces: %d.\n", getpid());

			handler(client); //comunicarea efectiva cu clientul

			close (client);
			exit(0);
		}
	}
	return 0;
}

void handler(int client_desc) {
	char buffer[MAX_LEN];
	while(1) {
		read_from_client(buffer);
		parse_command(buffer); //functie ce parseaza comanda
		write_to_client(buffer);
	}
}

//functie ce parseaza comanda din buffer si 
//intoarce un mesaj de raspuns tot prin parametrul buffer
void parse_command(char* buffer) {
	if(strncmp(buffer, "quit", 4) == 0) {
		printf("Clientul %d s-a deconectat.\n", getpid());
		strcpy(buffer, "quit|Deconectare...");
	} else if(strncmp(buffer, "help", 4) == 0) {
		cmd_help(buffer);
	} else if(strncmp(buffer, "login", 5) == 0) {
		cmd_login(buffer);
	} else if(strncmp(buffer, "logout", 6) == 0) {
		cmd_logout(buffer);
	} else if(strncmp(buffer, "upload", 6) == 0) {
		cmd_upload(buffer);
	} else if(strncmp(buffer, "list", 4) == 0) {
		cmd_list(buffer);
	} else if(strncmp(buffer, "cwd", 3) == 0) {
		cmd_cwd(buffer);
	} else if(strncmp(buffer, "cd", 2) == 0) {
		cmd_cd(buffer);
	} else if(strncmp(buffer, "newdir", 6) == 0) {
		cmd_newdir(buffer);
	} else if(strncmp(buffer, "newfile", 7) == 0) {
		cmd_newfile(buffer);
	} else if(strncmp(buffer, "rename", 6) == 0) {
		cmd_rename(buffer);
	} else if(strncmp(buffer, "delete", 6) == 0) {
		cmd_delete(buffer);
	} else {
		strcpy(buffer, "Comanda necunoscuta. Scrie `help` pentru a vedea lista de comenzi.");
	}
}

void cmd_help(char* buffer) {
	strcpy(buffer, "Comenzi disponibile:\n");
	strcat(buffer, "\tquit - inchide conexiunea cu serverul\n");
	strcat(buffer, "\thelp - afiseaza lista de comenzi\n");
	strcat(buffer, "\tlogin <username> - conecteaza un utilizator\n");
	strcat(buffer, "\tlogout - deconecteaza utilizatorul curent\n");
	strcat(buffer, "\tupload <filename> - incarca un fisier pe server\n");
	strcat(buffer, "\tcwd - afiseaza directorul curent\n");
	strcat(buffer, "\tcd <dirname> - schimba directorul de lucru\n");
	strcat(buffer, "\tlist - afiseaza fisierele din directorul curent\n");
	strcat(buffer, "\tnewdir <dirname> - creaza un director\n");
	strcat(buffer, "\tnewfile <filename> - creaza un fisier\n");
	strcat(buffer, "\trename <oldname> <newname> - redenumeste un fisier sau un director\n");
	strcat(buffer, "\tdelete <filename> - sterge un fisier sau un director\n");
}

void cmd_login(char* buffer) {
	//verificare sintaxa
	strtok(buffer, " ");
	char* tmp = strtok(NULL, " ");
	if(tmp == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: login <username>");
		return;
	}
	char* username = malloc(MAX_LEN);
	strcpy(username, tmp);
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: login <username>");
		return;
	}

	//verificare stare autentificare
	if(auth_state == 1) {
		strcpy(buffer, "Sunteti deja autentificat.");
		return;
	}

	//verificare existenta utilizator
	char* password = get_password_hash(username);
	if(password == NULL) {
		strcpy(buffer, "Utilizator inexistent.");
		return;
	}

	//verificare parola
	int tries = 0;
	strcpy(tmp, "await_password|Autentificare in contul "); //mesaj cu prefix special pentru client
	strcat(tmp, username);
	strcat(tmp, "...");
	write_to_client(tmp);
	do{
		read_from_client(buffer);
		tries++;

		if(strncmp(buffer, password, 64) != 0){
			if(tries == 3){ //blacklist dupa 3 incercari esuate
				write_to_client("quitting|Prea multe incercari esuate. Se inchide conexiunea...");
				close(client);
				exit(0);	
			}
			write_to_client("await_password|Parola gresita. Incercati din nou.");
		} else {
			auth_state = 1;
			strcpy(buffer, "Autentificat cu succes.");

			//initializare spatiu de stocare client
			struct stat s;
			char* client_dir = malloc(MAX_LEN);
			sprintf(client_dir, "storage/%s", username);
			if(stat(client_dir, &s) == -1 || !S_ISDIR(s.st_mode)) {
				if(mkdir(client_dir, 0700) == -1) {
					perror("Eroare la initializarea spatiului de stocare client");
					return;
				}
			}
			client_current_dir = malloc(MAX_LEN); //directorul curent de lucru al clientului
			strcpy(client_current_dir, client_dir);

			return;
		}
	} while(1);
}

void cmd_logout(char* buffer) {
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}
	auth_state = 0;
	client_current_dir = NULL; //resetam directorul curent de lucru al clientului
	strcpy(buffer, "Deconectat cu succes.");
}

void cmd_upload(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* filename = strtok(NULL, " ");
	if(filename == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: upload <filename>");
		return;
	}
	filename = strdup(filename);
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: upload <filename>");
		return;
	}

	//verificare existenta fisier in directorul clientului
	char* path = malloc(MAX_LEN);
	sprintf(path, "%s/%s", client_current_dir, filename);
	struct stat s;
	if(stat(path, &s) != -1) {
		strcpy(buffer, "Fisierul exista deja!");
		return;
	}

	//trimitere confirmare incepere upload
	strcpy(buffer, "await_upload|");
	strcat(buffer, filename); //trimitem si numele fisierului
	write_to_client(buffer);

	//citire dimensiune fisier
	read_from_client(buffer);
	if(strncmp(buffer, "cancel", 6) == 0) { //clientul a anulat uploadul
		strcpy(buffer, "Upload anulat.");
		return;
	}
	int file_size = atoi(buffer);

	//numele fisierului este ultimul cuvant din filename
	char* tmp = strtok(filename, "/");
	while(tmp != NULL) {
		filename = tmp;
		tmp = strtok(NULL, "/");
	}
	sprintf(path, "%s/%s", client_current_dir, filename);

	//creare fisier
	FILE* file = fopen(path, "w");
	if(file == NULL) {
		strcpy(buffer, "cancel_upload");
		perror("Eroare la crearea fisierului pentru upload");
		return;
	}

	write_to_client("ready"); //confirmare incepere upload
	int bytes_read = 0;
	while(bytes_read < file_size) {
		int len = read(client, buffer, MAX_LEN); //citim din socket
		if(fwrite(buffer, 1, len, file) == -1) { //scriem in fisier
			sprintf(buffer, "Eroare la upload. S-au citit %d bytes din totalul de %d.", bytes_read, file_size);
			perror("Eroare la upload");
			fclose(file);
			return;
		}
		bytes_read += len;
	}
	fclose(file);
	sprintf(buffer, "Upload terminat. S-au citit %d bytes din totalul de %d.", bytes_read, file_size);
}

void cmd_cwd(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	if(strtok(NULL, " ") != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: cwd");
		return;
	}

	strcpy(buffer, "Va aflati in directorul `");
	strcat(buffer, client_current_dir); //directorul curent de lucru al clientului
	strcat(buffer, "`.");
}

void cmd_cd(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* dirname = strtok(NULL, " ");
	if(dirname == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: cd <director>");
		return;
	}
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: cd <director>");
		return;
	}

	//verificare ca directorul sa nu fie intr-un loc in afara spatiului de stocare
	int depth = 1;
	char* tmp = malloc(MAX_LEN);
	strcpy(tmp, client_current_dir);
	while(strstr(tmp, "/") != NULL) { //numaram adancimea directorului curent
		depth++;
		tmp = strstr(tmp, "/") + 1;
	}
	strcpy(tmp, dirname);
	char* new_path = malloc(MAX_LEN); //construim calea noua
	strcpy(new_path, client_current_dir);
	char* p = strtok(tmp, "/");
	while(p != NULL) { //numaram adancimea directorului nou
		if(strcmp(p, "..") == 0) {
			char* q = strrchr(new_path, '/');
			if(q != NULL) {
				*q = '\0';
			}
			depth--;
		} else {
			strcat(new_path, "/");
			strcat(new_path, p);
			depth++;
		}
		p = strtok(NULL, "/");
	}
	if(depth < 2) { //se incearca iesirea din storage
		strcpy(buffer, "Nu puteti accesa un director din afara spatiului de stocare.");
		return;
	}

	// verificare existenta directorului nou
	struct stat s;
	if(stat(new_path, &s) == -1 || !S_ISDIR(s.st_mode)) {
		strcpy(buffer, "Directorul nu exista.");
		return;
	}

	//schimbare director
	strcpy(client_current_dir, new_path); //actualizam directorul curent de lucru al clientului
	strcpy(buffer, "Director schimbat cu succes.");
}

void cmd_list(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	if(strtok(NULL, " ") != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: list");
		return;
	}

	DIR* dir = opendir(client_current_dir); //deschidem directorul curent de lucru al clientului
	if(dir == NULL) {
		perror("Eroare la deschiderea directorului curent");
		return;
	}

	//listare fisiere
	char* tmp = malloc(MAX_LEN);
	strcpy(tmp, "Fisiere in directorul curent, `");
	strcat(tmp, client_current_dir);
	strcat(tmp, "`:");
	int files = 0;
	struct dirent* entry;
	while((entry = readdir(dir)) != NULL) { //pentru fiecare fisier din director
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		files++;
		strcat(tmp, "\n\t");
		strcat(tmp, entry->d_name);

		//daca e director adaugam un `/` la sfarsit
		struct stat s;
		char* path = malloc(MAX_LEN);
		sprintf(path, "%s/%s", client_current_dir, entry->d_name);
		if(stat(path, &s) == -1) {
			perror("Eroare la verificarea tipului de fisier");
			return;
		}
		if(S_ISDIR(s.st_mode)) {
			strcat(tmp, "/");
		}
	}
	closedir(dir);
	if(!files) {
		strcat(tmp, "\nDirectorul este gol.");
	}
	strcpy(buffer, tmp);
}

void cmd_newdir(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* dirname = strtok(NULL, " ");
	if(dirname == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: newdir <director>");
		return;
	}
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: newdir <director>");
		return;
	}

	//verificare ca directorul sa nu fie intr-un loc in afara spatiului de stocare
	if(strstr(dirname, "..") != NULL) {
		strcpy(buffer, "Numele directorului nu poate contine `..`.");
		return;
	}

	//creare director
	char* path = malloc(MAX_LEN);
	sprintf(path, "%s/%s", client_current_dir, dirname);
	if(mkdir(path, 0700) == -1) {
		if(errno == EEXIST) {
			strcpy(buffer, "Directorul exista deja.");
		} else if (errno == ENOENT) {
			strcpy(buffer, "Directorul parinte nu exista.");
		} else {
			strcpy(buffer, "quit|O eroare a aparut la crearea directorului.");
			perror("Eroare la crearea directorului");
		}
		return;
	}
	strcpy(buffer, "Director creat cu succes.");
}

void cmd_newfile(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* filename = strtok(NULL, " ");
	if(filename == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: newfile <filename>");
		return;
	}
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: newfile <filename>");
		return;
	}

	//verificare ca fisierul sa nu fie intr-un loc in afara spatiului de stocare
	if(strstr(filename, "..") != NULL || strstr(filename, "/") != NULL) {
		strcpy(buffer, "Numele fisierului nu poate contine `/` sau `..`.");
		return;
	}

	//verificare ca fisierul sa nu existe deja
	char* path = malloc(MAX_LEN);
	sprintf(path, "%s/%s", client_current_dir, filename);
	struct stat s;
	if(stat(path, &s) != -1) {
		strcpy(buffer, "Fisierul exista deja.");
		return;
	}

	//creare fisier
	FILE* f = fopen(path, "w");
	if(f == NULL) {
		strcpy(buffer, "quit|O eroare a aparut la crearea fisierului.");
		perror("Eroare la crearea fisierului");
		return;
	}
	fclose(f);
	strcpy(buffer, "Fisier creat cu succes.");
}

void cmd_rename(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* oldname = strtok(NULL, " ");
	char* newname = strtok(NULL, " ");
	if(oldname == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: rename <oldname> <newname>");
		return;
	}
	if(newname == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: rename <oldname> <newname>");
		return;
	}
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: rename <oldname> <newname>");
		return;
	}

	//verificare ca fisierul sa fie in directorul curent
	if(strstr(oldname, "/") != NULL || strstr(newname, "/") != NULL || strstr(oldname, "..") != NULL || strstr(newname, "..") != NULL) {
		strcpy(buffer, "Puteti redenumi doar fisiere / directoare din directorul curent de lucru.");
		return;
	}

  //verificare ca fisierul sa existe
	char* path = malloc(MAX_LEN);
	sprintf(path, "%s/%s", client_current_dir, oldname);
	struct stat s;
	if(stat(path, &s) == -1 || (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode))) {
		strcpy(buffer, "Fisierul / Directorul nu exista.");
		return;
	}

	//redenumire
	sprintf(path, "%s/%s", client_current_dir, oldname);
	char* newpath = malloc(MAX_LEN);
	sprintf(newpath, "%s/%s", client_current_dir, newname);
	if(rename(path, newpath) == -1) {
		if(errno == EEXIST) {
			strcpy(buffer, "Numele nou exista deja.");
		} else if (errno == ENOENT) {
			strcpy(buffer, "Fisierul / Directorul parinte nu exista.");
		} else {
			strcpy(buffer, "quit|O eroare a aparut la redenumirea fisierului / directorului.");
			perror("Eroare la redenumirea fisierului / directorului");
		}
		return;
	}
  //verificare tip fisier / director
	stat(path, &s);
	if(S_ISREG(s.st_mode)) {
		strcpy(buffer, "Fisier redenumit cu succes.");
	} else {
		strcpy(buffer, "Director redenumit cu succes.");
	}
}

void cmd_delete(char* buffer) {
	//verificare stare autentificare
	if(auth_state == 0) {
		strcpy(buffer, "Nu sunteti autentificat.");
		return;
	}

	//validare comanda
	strtok(buffer, " ");
	char* filename = strtok(NULL, " ");
	if(filename == NULL) {
		strcpy(buffer, "Prea putine argumente! Corect: delete <filename>");
		return;
	}
	char* excess = strtok(NULL, " ");
	if(excess != NULL) {
		strcpy(buffer, "Prea multe argumente! Corect: delete <filename>");
		return;
	}

	//verificare ca fisierul sa fie in directorul curent
	if(strstr(filename, "/") != NULL || strstr(filename, "..") != NULL) {
		strcpy(buffer, "Puteti sterge doar fisiere / directoare din directorul curent de lucru.");
		return;
	}

	//stergere
	char* path = malloc(MAX_LEN);
	sprintf(path, "%s/%s", client_current_dir, filename);
	struct stat s;
	if(stat(path, &s) == -1 || (!S_ISREG(s.st_mode) && !S_ISDIR(s.st_mode))) {
		strcpy(buffer, "Fisierul / Directorul nu exista.");
		return;
	}

	if(S_ISDIR(s.st_mode)) { //director
		if(rmdir(path) == -1) {
			if(errno == ENOTEMPTY) {
				strcpy(buffer, "Directorul nu este gol.");
			} else {
				strcpy(buffer, "quit|O eroare a aparut la stergerea directorului.");
				perror("Eroare la stergerea directorului");
			}
			return;
		}
		strcpy(buffer, "Director sters cu succes.");
	} else { //fisier
		if(unlink(path) == -1) {
			strcpy(buffer, "quit|O eroare a aparut la stergerea fisierului.");
			perror("Eroare la stergerea fisierului");
			return;
		}
		strcpy(buffer, "Fisier sters cu succes.");
	}
}

//functie ce intoarce hash-ul parolei unui utilizator primit ca parametru
char* get_password_hash(char* username) {
	FILE* f = fopen("users.txt", "r");
	if(f == NULL) {
		perror("Eroare la deschiderea fisierului users.txt");
		return NULL;
	}
	char* line = malloc(MAX_LEN);
	while(fgets(line, MAX_LEN, f) != NULL) {
		char* user = strtok(line, " ");
		if(strcmp(user, username) == 0) {
			char* password = strtok(NULL, "\n");
			fclose(f);
			return password;
		}
	}
	fclose(f);
	return NULL;
}

//functia de citire in socket
void read_from_client(char* buffer) {
	bzero(buffer, MAX_LEN);
	if(read(client, buffer, MAX_LEN) < 0) {
		perror("Eroare la citirea de la client");
		return;
	}
	//eliminam \n de la sfarsitul comenzii
	if(buffer[strlen(buffer) - 1] == '\n') {
		buffer[strlen(buffer) - 1] = '\0';
	}
}

//functia de scriere in socket
void write_to_client(char* buffer) {
	if(write(client, buffer, MAX_LEN) < 0) {
		perror("Eroare la scrierea catre client");
		return;
	}
}
