import logging
import re
from spyne import Application, ServiceBase, Unicode, rpc
from spyne.protocol.soap import Soap11
from spyne.server.wsgi import WsgiApplication
from wsgiref.simple_server import make_server

class ConversorMensajes(ServiceBase):
    
    # Recibe un Unicode (string) y devuelve un Unicode
    @rpc(Unicode, _returns=Unicode)
    def convertir(ctx, mensaje):
        if not mensaje:
            return ""
        
        # Conviertimos saltos de línea y múltiples espacios en un solo espacio
        mensaje_formateado = re.sub(r'\s+', ' ', mensaje)
        
        return mensaje_formateado

# Configuramos la aplicación Spyne
application = Application(
    services=[ConversorMensajes],
    tns='http://conversor.mensajes/',
    in_protocol=Soap11(validator='lxml'),
    out_protocol=Soap11()
)

application = WsgiApplication(application)

if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)
    logging.getLogger('spyne.protocol.xml').setLevel(logging.DEBUG)

    logging.info(f"Escuchando en http://127.0.0.1:8000")
    logging.info(f"El WSDL está en: http://localhost:8000/?wsdl")

    server = make_server('127.0.0.1', 8000, application)
    server.serve_forever()