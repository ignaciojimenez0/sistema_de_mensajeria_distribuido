#include <stdio.h>
#include <stdlib.h>
#include "servidor_logs.h"

int call_rpc_log(char *user, char *op, char *filename) {
    CLIENT *cliente;
    enum clnt_stat retval; 
    int resultado_salida;
    struct Log info;
    char *servidor;

    servidor = getenv("LOG_RPC_IP");
    if (servidor == NULL) {
        return -1;
    }

    cliente = clnt_create(servidor, LOG, 1, "tcp");
    if (cliente == NULL) {
        clnt_pcreateerror(servidor);
        return -1;
    }

    info.usuario = user;
    info.operacion = op;
    
    if (filename) {
        info.fichero = filename;
    } else {
        info.fichero = "";
    }

    retval = imprimir_log_1(info, &resultado_salida, cliente);
    
    if (retval != RPC_SUCCESS) {
        clnt_perror(cliente, "call failed");
        clnt_destroy(cliente);
        return -1;
    }

    clnt_destroy(cliente);
    return 0;
}