/*Ejecucion:
    cd /mnt/c/Users/Alan/Desktop/Ejercicio1/
    gcc main.c -o programa -lm
    ./programa --negativo --tonalidad-verde --tonalidad-roja --tonalidad-azul
*/
#include "main.h"
#include "estructuras.h"
#include "constantes.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <dirent.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <sys/shm.h>
#include <sys/ipc.h>

#include <fcntl.h>// Para O_CREAT y O_EXCL
#include <errno.h>
sem_t* sem; // semáforo global para hijos
pid_t pids[CANTIDAD_HIJOS];// PID de hijos
int shmid;//Memoria compartida
t_datos_compartidos* datos;//Datos compartidos
int hijos_vivos[CANTIDAD_HIJOS] = {1, 1, 1, 1}; // global, inicializada en 1

int main(int argc, char* argv[])
{
    signal(SIGINT, manejador_sigint);
    signal(SIGCHLD, manejador_padre);
    signal(SIGTERM, manejador_finalizacion);

    int instruccionesInt[argc - 1];
    int cantInstrucciones = 0;
    struct dirent* entrada;
    //validar instrucciones recibidas
    if (argc != 5) {
        fprintf(stderr, "Error: se esperaban 4 instrucciones, se recibieron %d.\n", argc - 1);
        return FALTAN_INSTRUCCIONES;
    }
    for(int i = 1; i < argc; i++){
        int id = buscarInstrucciones(argv[i]);
        if (id == INSTRUCCION_INVALIDA){
            fprintf(stderr, "Error: instruccion '%s' no válida.\n", argv[i]);
            return INSTRUCCION_INVALIDA;
        }

        instruccionesInt[cantInstrucciones++] = id;
    }
    // Crear el semáforo
    sem = sem_open("/mutex", O_CREAT | O_EXCL, 0600, 1);
    if (sem == SEM_FAILED) {
        perror("Error al crear semáforo");
        sem_unlink("/mutex"); // por si quedo colgado de otra ejecucion
        sem = sem_open("/mutex", O_CREAT, 0600, 1);
        if (sem == SEM_FAILED) {
            printf("No se pudo crear semáforo luego del unlink");
            return ERROR_SEM;
        }
    }
    // Crear memoria compartida
    shmid = shmget(CLAVE_MEMORIA, sizeof(t_datos_compartidos), IPC_CREAT | 0666);
    if (shmid == -1) {
        printf("Error creando memoria compartida");
        return ERROR_MEM_COMP;
    }
    datos = (t_datos_compartidos*)shmat(shmid, NULL, 0);
    if (datos == (void*)-1) {
        printf("Error al unir memoria compartida");
        return ERROR_MEM_COMP;
    }
    datos->finalizar = 0;
    datos->nuevaTarea = 0;
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        sem_init(&datos->semTareaCompletada[i], 1, 0); // Inicializa en 0
    }
    printf("Presione ENTER para ejecutar...\n");
    getchar();

    //CREAR HIJOS
    if(crearHijos(pids, datos) != TODO_OK) {
        limpiarMemySem(shmid, datos);
        return HIJOS_NO_CREADOS;
    }
    //PROCESO PADRE
    DIR* dir = opendir("./Imagenes"); //opendir para leer las imagenes de esa carpeta
    if (!dir) {
        finalizarHijos();
        limpiarMemySem(shmid,datos);
        printf("No se pudo abrir la carpeta");
        return CARPETA_NO_ENCONTRADA;
    }
    while ((entrada = readdir(dir)) != NULL) {//mientras lea algo
        if (strstr(entrada->d_name, ".bmp") == NULL //si no es .bmp
            || strcmp(entrada->d_name, ".") == 0 //si es una carpeta
            || strcmp(entrada->d_name, "..") == 0) // si es el directorio padre
            continue; //salta a la siguiente lectura

        char rutaOriginal[MAX_BUFFER];
        snprintf(rutaOriginal, sizeof(rutaOriginal), "./Imagenes/%s", entrada->d_name);
        printf("\nRuta original: %s\n",rutaOriginal);

        if(lecturaCabecera(&datos->meta, rutaOriginal) != TODO_OK)
            continue;

        for (int i = 0; i < CANTIDAD_HIJOS; i++) {
            if (!estaVivo(pids[i])) {
                printf("Hijo %d está muerto, saltando...\n", i);
                continue;
            }
            sem_wait(sem);
            strncpy(datos->ruta, rutaOriginal, MAX_BUFFER);
            datos->instruccion = instruccionesInt[i];
            datos->idHijo = i;
            datos->nuevaTarea = 1;
            sem_post(sem);
            // Esperar a que el hijo notifique que termino
            while (sem_wait(&datos->semTareaCompletada[i]) == -1){
                if (errno == EINTR &&!estaVivo(pids[i])){
                    printf("Padre: hijo %d murio mientras procesaba. Continuando...\n", i);
                    break;
                }
                continue;
            }
        }
    }
    closedir(dir);
    printf("\nPresione ENTER para finalizar...\n");
    getchar();

    // Limpieza del semáforo
    sem_wait(sem);
    datos->finalizar = 1;
    sem_post(sem);
    //Finaliza procesos hijos
    finalizarHijos();
    //Limpieza de datos
    limpiarMemySem(shmid,datos);

    printf("\nPadre: Todos los hijos han finalizado\n");
    return TODO_OK;
}

void limpiarMemySem(int shmid, t_datos_compartidos*datos){
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        sem_destroy(&datos->semTareaCompletada[i]);
    }
    shmdt(datos);
    shmctl(shmid, IPC_RMID, NULL);
    sem_close(sem);
    sem_unlink("/mutex");
}

int estaVivo(pid_t pid) {
    return !(kill(pid, 0) == -1 && errno == ESRCH);
}

void manejador_hijo(int sig) {
    printf("Hijo %d recibio senal %d, cerrando recursos...\n", getpid(), sig);
    if (sem) {
        // Intentar hacer sem_post para desbloquear al padre
        if (sem_post(sem) == -1) {
            perror("sem_post en manejador_hijo");
        }
        sem_close(sem);
    }
    exit(0);
}

void manejador_sigint(int sig) {
    printf("Padre: senal de interrupcion recibida.\n");
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        kill(pids[i], SIGTERM);
    }
    sleep(1);
    limpiarMemySem(shmid, datos);
    exit(0);
}

void manejador_padre(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < CANTIDAD_HIJOS; i++) {

            if (pids[i] == pid) {
                hijos_vivos[i] = 0;
                if (WIFSIGNALED(status)) {
                    printf("Padre: hijo %d termino por senal %d\n", pid, WTERMSIG(status));
                } else if (WIFEXITED(status)) {
                    printf("Padre: hijo %d termino normalmente\n", pid);
                } else {
                    printf("Padre: hijo %d termino inesperadamente\n", pid);
                }
                break;
            }
        }
    }
}

void manejador_finalizacion(int sig) {
    printf("\nPadre: senal %d recibida, limpiando recursos...\n", sig);
    // Avisar a los hijos que terminen
    if (datos != NULL)
        datos->finalizar = 1;
    // Terminar hijos vivos
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        if (pids[i] > 0)
            kill(pids[i], SIGTERM);
    }
    sleep(1); // dar tiempo a los hijos a cerrar
    // Limpieza de recursos
    limpiarMemySem(shmid, datos); // usa variable global si es necesario
    exit(0);
}

void finalizarHijos(){
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        if (pids[i] > 0) {
            int status;
            pid_t result = waitpid(pids[i], &status, 0);
            if (result != -1){
                if (WIFEXITED(status)) {
                    printf("Padre: hijo %d termino normalmente\n", pids[i]);
                } else if (WIFSIGNALED(status)) {
                    printf("Padre: hijo %d termino por senal %d\n", pids[i], WTERMSIG(status));
                } else {
                    printf("Padre: hijo %d termino de forma desconocida\n", pids[i]);
                }
            }
        }
    }
}

int crearHijos(pid_t pids[], t_datos_compartidos* datos) {
    for (int i = 0; i < CANTIDAD_HIJOS; i++) {
        if ((pids[i] = fork()) == 0) {
            signal(SIGTERM, manejador_hijo);

            sem_t* sem = sem_open("/mutex", 0);
            if (sem == SEM_FAILED) {
                printf("\nsem_open en hijo\n");
                return ERROR_SEM;
            }
            while (1) {
            //espera a que el semaforo este libre, si otro hijo lo tiene queda bloqueado
                while (sem_wait(sem) == -1) {
                    if (errno == EINTR) continue;
                    perror("Hijo: sem_wait error");
                    exit(1);
                }
                if (datos->finalizar) {
                    sem_post(sem);
                    break;
                }
                if (datos->nuevaTarea && datos->idHijo == i) {
                    printf("Hijo %d: procesando %s con instruccion %d\n", i, datos->ruta, datos->instruccion);
                    // Region crítica: un solo hijo pinta a la vez
                    pintarPixeles(&datos->meta, datos->ruta, datos->instruccion);//region critica
                    datos->nuevaTarea = 0;
                    // Notificar al padre que termino
                    sem_post(&datos->semTareaCompletada[i]);
                }
                sem_post(sem);//libera el semaforo. aumenta en 1
                usleep(50000); // duerme 50 ms
            }
            sem_close(sem);//libera recursos locales asociados al semaforo que uso el proceso hijo
            exit(0);
        }
        if (pids[i] < 0) {
            perror("Error al crear hijo");
            return HIJOS_NO_CREADOS;
        }
    }
    return TODO_OK;
}

int lecturaCabecera(t_metadata*meta, char*nomArch)
{
    FILE*pf = fopen(nomArch, "rb"); //Abre el archivo originalBmp
    if(!pf)
        return ARCHIVO_NO_ENCONTRADO;
    //Empieza a leer el encabezado del archivo original
    fseek(pf, 28, SEEK_SET);
    fread(&meta->profundidad, sizeof(unsigned short), 1, pf);

    if(meta->profundidad != 24)
    {
        printf("No es de 24 bits: %d\n", meta->profundidad);
        fclose(pf);
        return NO_ES_24BITS;
    }

    fseek(pf, 2, SEEK_SET);
    fread(&meta->tamArchivo, sizeof(unsigned int), 1, pf);

    fseek(pf, 10, SEEK_SET);
    fread(&meta->comienzoImagen, sizeof(unsigned int), 1, pf);

    fseek(pf, 14, SEEK_SET);
    fread(&meta->tamEncabezado, sizeof(unsigned int), 1, pf);

    fseek(pf, 18, SEEK_SET);
    fread(&meta->ancho, sizeof(unsigned int), 1, pf);

    fseek(pf, 22, SEEK_SET);
    fread(&meta->alto, sizeof(unsigned int), 1, pf);

    fclose(pf);
    return TODO_OK;
}

int pintarPixeles(t_metadata*meta, char* origin,int instruccion)
{
    t_pixel pixel;
    FILE* original = fopen(origin, "rb");
    if (!original)
        return ARCHIVO_NO_ENCONTRADO;
    char* rutaNuevo = crearArchivo(origin,instruccion,meta);
    if(!rutaNuevo){
        fclose(original);
        printf("\n\nError inesperado al abrir el archivo creado\n\n.");
        return ARCHIVO_NO_ENCONTRADO;

    }
    FILE* nuevo = fopen(rutaNuevo,"r+b");
    if (!nuevo) {
        free(rutaNuevo);
        fclose(original);
        printf("\n\nError inesperado al abrir el archivo creado\n\n.");
        return ARCHIVO_NO_ENCONTRADO;
    }

    unsigned char buffer[meta->comienzoImagen];
    fread(buffer, 1, meta->comienzoImagen, original);
    fwrite(buffer, 1, meta->comienzoImagen, nuevo);
    fseek(original, meta->comienzoImagen, SEEK_SET);
    fseek(nuevo, meta->comienzoImagen, SEEK_SET);

    while(fread(&pixel,1,3,original)==3)
    {
        switch(instruccion){
            case 0: negativo(&pixel,nuevo);
                break;
            case 1: escalaGrises(&pixel,nuevo);
                break;
            case 2: cambiarContraste(&pixel,nuevo,10);
                break;
            case 3: cambiarContraste(&pixel,nuevo,-10);
                break;
            case 4:
            case 5:
            case 6: cambiarTonalidad(&pixel,nuevo,instruccion-4);//se manda instruccion por parametro y se le resta 4 para ubicar bien el vector.
                break;
            default: printf("Error desconocido");
        }
        fwrite(&pixel,1,3,nuevo);
    }
    printf("¡Imagen %s generada con exito!\n",rutaNuevo);
    free(rutaNuevo);
    fclose(nuevo);
    fclose(original);
    return TODO_OK;
}


void negativo(t_pixel *pixel,FILE*nuevo)
{
    for(int i=0;i<3;i++)
    {
        pixel->pixel[i] = 255 - pixel->pixel[i];
    }
}

void escalaGrises(t_pixel *pixel,FILE*nuevo)
{
    int acum=0;
    for(int i=0;i<CANTIDAD_COLORES;i++)
    {
        acum += pixel->pixel[i];
        if(i == CANTIDAD_COLORES-1)
            for(int j = 0; j<CANTIDAD_COLORES; j++)
                pixel->pixel[j] = acum / CANTIDAD_COLORES;
    }
}

void cambiarContraste(t_pixel *pixel,FILE*nuevo,int valor)
{
    for(int j = 0; j < CANTIDAD_COLORES; j++)
    {
        if (pixel->pixel[j] < 100)
            pixel->pixel[j] = fmax(0, pixel->pixel[j] - valor);
        else if (pixel->pixel[j] > 155)
            pixel->pixel[j] = fmin(255, pixel->pixel[j] + valor);
    }
}

void cambiarTonalidad(t_pixel *pixel,FILE*nuevo,int tipo)
{
    for(int i=0;i<3;i++)
    {
        if(pixel->pixel[tipo]<205 && tipo==i) pixel->pixel[tipo]+= 50;
        else if(pixel->pixel[i]>10 && i!=tipo){
            pixel->pixel[i]-= 10;
        }
    }
}

char* crearArchivo(char* pathOriginal, int instruccionIdx, t_metadata*meta) {
    char* nombresInstr[] = {
        "negativo", "escala-de-grises", "aumentar-contraste",
        "reducir-contraste", "tonalidad-azul", "tonalidad-verde", "tonalidad-roja"
    };

    // Extraer solo el nombre del archivo sin ruta
    const char* nombreBase = strrchr(pathOriginal, '/');
    if (!nombreBase) nombreBase = pathOriginal;
    else nombreBase++; // salteamos la barra

    size_t len = strlen(nombreBase);
    char* fuenteRes = malloc(len - 3);
    strncpy(fuenteRes, nombreBase, len - 4);
    fuenteRes[len - 4] = '\0';

    char* nomArchivo = malloc(512);
    if (!nomArchivo) {
        free(fuenteRes);
        return NULL;
    }
    sprintf(nomArchivo, "./Generadas/%s-%s.bmp", fuenteRes, nombresInstr[instruccionIdx]);

    FILE*pf = fopen(nomArchivo, "rb");
    int cont = 1;
    while (pf) {
        fclose(pf);
        sprintf(nomArchivo, "./Generadas/%s-%s%d.bmp", fuenteRes, nombresInstr[instruccionIdx], cont++);
        pf = fopen(nomArchivo, "rb");
    }
    free(fuenteRes);

    pf = fopen(nomArchivo, "w+b");
    FILE*original = fopen(pathOriginal,"rb");
    if (!pf || !original) {
        printf("Error al crear archivo %s\n", nomArchivo);
        return NULL;
    }
    cargarMetadata(original,pf,meta);
    printf("Copia %s creada con exito\n",nombresInstr[instruccionIdx]);
    fclose(pf);
    fclose(original);
    return nomArchivo;
}
void cargarMetadata(FILE*originalBmp,FILE*nuevo,t_metadata*meta)
{
    fseek(originalBmp,0,SEEK_SET);
    unsigned char buffer[meta->comienzoImagen];
    fread(buffer, 1, meta->comienzoImagen, originalBmp);
    fwrite(buffer, 1, meta->comienzoImagen, nuevo);
}

unsigned int buscarInstrucciones(char* inst){
    char instrucciones[7][21]={"--negativo", //estas son todas las instrucciones que puede mandar el usuario
                            "--escala-de-grises",
                            "--aumentar-contraste",
                            "--reducir-contraste",
                            "--tonalidad-azul",
                            "--tonalidad-verde",
                            "--tonalidad-roja"};
    int i = 0;
    while(i < 7){
        if(strcmp(instrucciones[i],inst)==0){
            printf("Instruccion valida\n");
            return i;
        }
        i++;
    }
    return INSTRUCCION_INVALIDA;
}
