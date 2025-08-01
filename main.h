#include "estructuras.h"
#include "constantes.h"

#include <unistd.h>
#include <stdio.h>

int lecturaCabecera(t_metadata*,char*);
int pintarPixeles(t_metadata*,char* ,int );
void cambiarTonalidad(t_pixel *pixel,FILE*fichero,int tipo);
void cambiarContraste(t_pixel *pixel,FILE*fichero,int tipo);
void escalaGrises(t_pixel *pixel,FILE*fichero);
void negativo(t_pixel *pixel,FILE*fichero);
char* crearArchivo(char*,int,t_metadata*);
void cargarMetadata(FILE*bmp,FILE*fichero,t_metadata*meta);
unsigned int buscarInstrucciones(char*inst);

void limpiarMemySem(int shmid, t_datos_compartidos* datos);
int estaVivo(pid_t pid);
void manejador_hijo(int sig);
void manejador_sigint(int sig);
void manejador_padre(int sig);
void manejador_finalizacion(int sig) ;
void finalizarHijos();
int crearHijos(pid_t pids[], t_datos_compartidos*);
