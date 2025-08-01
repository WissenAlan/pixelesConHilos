#ifndef ESTRUCTURAS_H_INCLUDED
#define ESTRUCTURAS_H_INCLUDED
#include "constantes.h"
#include <semaphore.h> // IMPORTANTE
typedef struct
{
    unsigned char pixel[3];
    unsigned int profundidad;  // Esta estructura admite formatos de distinta profundidad de color, a priori utilizaremos sólo 24 bits.
}t_pixel;

typedef struct
{
    unsigned int tamArchivo;
    unsigned int tamEncabezado;    // El tamaño del encabezado no siempre coincide con el comienzo de la imagen
    unsigned int comienzoImagen;   // Por eso dejo espacio para ambas cosas
    unsigned int ancho;
    unsigned int alto;
    unsigned short profundidad;
}t_metadata;

typedef struct {
    char ruta[MAX_BUFFER];
    int instruccion;
    int nuevaTarea;
    int finalizar;
    t_metadata meta;
    int idHijo;
    sem_t semTareaCompletada[CANTIDAD_HIJOS];//cada hijo tiene un semaforo de finalizacion
} t_datos_compartidos;

#endif // ESTRUCTURAS_H_INCLUDED
