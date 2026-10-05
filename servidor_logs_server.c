#include "servidor_logs.h"
#include <stdio.h>

bool_t
imprimir_log_1_svc(struct Log arg1, int *result, struct svc_req *rqstp)
{
    /* Si es SENDATTACH */
    if (strcmp(arg1.operacion, "SENDATTACH") == 0 && strlen(arg1.fichero) > 0) {
        printf("%s %s %s\n", arg1.usuario, arg1.operacion, arg1.fichero);
    } else { /* Para el resto de operaciones */
        printf("%s %s\n", arg1.usuario, arg1.operacion);
    }
    fflush(stdout);

    *result = 0; /* Código de éxito */
    return TRUE;
}

int
log_1_freeresult (SVCXPRT *transp, xdrproc_t xdr_result, caddr_t result)
{
    xdr_free (xdr_result, result);
    return 1;
}