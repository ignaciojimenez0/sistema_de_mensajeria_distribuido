from enum import Enum
import argparse
import socket
import threading
import zeep


class client :

    # ******************** TYPES *********************
    # *
    # * @brief Return codes for the protocol methods
    class RC(Enum) :
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************
    # Variables
    _ip_servidor = None
    _puerto_servidor = -1
    _hilo_recibir = None
    _socket_recibir = None
    _puerto_recibir = None
    _nombre_usuario = None
    _hilo_corriendo = False
    _usuarios_conectados = {}  

    # Función auxiliar para leer un byte de respuesta del socket
    @staticmethod
    def _readByte(socket):
        byte = socket.recv(1)
        if not byte:
            return -1
        return ord(byte)

    # Función auxiliar para enviar un string
    @staticmethod
    def _sendString(socket, string):
        socket.sendall(string.encode())
        socket.sendall(b'\0')

    # Función auxiliar para leer un string terminado en \0
    @staticmethod
    def _readString(socket):
        resultado = ""
        while True:
            string = socket.recv(1)
            if string == b'\0' or not string:
                break
            resultado += string.decode()
        return resultado

    # Este hilo se queda escuchando en un puerto para recibir mensajes del servidor
    # También atiende peticiones GET FILE de otros clientes
    @staticmethod
    def _funcion_hilo_recibir():
        # Ponemos un timeout de 1 segundo al socket para evitar que accept se quede bloqueado infinitamente
        # De esta forma, si ponemos la variable de clase _hilo_corriendo a False, el bucle parará
        if client._socket_recibir is not None:
            client._socket_recibir.settimeout(1.0)

        while client._hilo_corriendo:
            try:
                socket_servidor, dir_servidor = client._socket_recibir.accept()

            except socket.timeout:
                continue
            except OSError:
                break
            except Exception:
                break

            try:
                # Leemos el tipo de operación
                tipo_op = client._readString(socket_servidor)

                if tipo_op == "SEND_MESSAGE":
                    # Recepción del mensaje
                    remitente = client._readString(socket_servidor)
                    id_mensaje = client._readString(socket_servidor)
                    mensaje = client._readString(socket_servidor)

                    print(f"\rs> MESSAGE {id_mensaje} FROM {remitente}")
                    print(mensaje)
                    print("END")
                    print("c> ", end="", flush=True)

                elif tipo_op == "SEND_MESS_ACK":
                    # Confirmación de entrega
                    id_mensaje = client._readString(socket_servidor)

                    print(f"\rc> SEND MESSAGE {id_mensaje} OK")
                    print("c> ", end="", flush=True)

                elif tipo_op == "SEND_MESSAGE_ATTACH":
                    # Recepción de mensaje con fichero adjunto
                    remitente = client._readString(socket_servidor)
                    id_mensaje = client._readString(socket_servidor)
                    mensaje = client._readString(socket_servidor)
                    fichero = client._readString(socket_servidor)

                    print(f"\rs> MESSAGE {id_mensaje} FROM {remitente}")
                    print(mensaje)
                    print("END")
                    print(f"FILE {fichero}")
                    print("c> ", end="", flush=True)

                elif tipo_op == "SEND_MESS_ATTACH_ACK":
                    # Confirmación de entrega con fichero adjunto
                    id_mensaje = client._readString(socket_servidor)
                    fichero = client._readString(socket_servidor)

                    print(f"\rc> SENDATTACH MESSAGE {id_mensaje} {fichero} OK")
                    print("c> ", end="", flush=True)

                elif tipo_op == "GET_FILE":
                    # Otro cliente nos pide un fichero, lo leemos y se lo enviamos
                    solicitante = client._readString(socket_servidor)
                    nombre_fichero = client._readString(socket_servidor)

                    try:
                        with open(nombre_fichero, "rb") as f:
                            datos = f.read()
                        socket_servidor.sendall(datos)
                    except Exception:
                        pass

            except Exception as e:
                pass

            finally:
                socket_servidor.close()

    # Esta función obtiene un puerto libre
    @staticmethod
    def _buscar_puerto_libre():
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.bind(('', 0))
            sock.listen(1)
            puerto = sock.getsockname()[1]
        return puerto

    # ******************** METHODS *******************
    # *
    # * @param user - User name to register in the system
    # *
    # * @return OK if successful
    # * @return USER_ERROR if the user is already registered
    # * @return ERROR if another error occurred
    @staticmethod
    def register(usuario):
        # Creamos el socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        try:
            # Conectamos el socket al servidor
            sock.connect((client._ip_servidor, client._puerto_servidor))

            # Enviamos tipo de operación
            client._sendString(sock, "REGISTER")

            # Enviamos el nombre de usuario
            client._sendString(sock, usuario)

            # Recibimos resultado
            resultado = client._readByte(sock)

            # Interpretamos la respuesta recibida
            if resultado == 0:
                print('c> REGISTER OK')
                return client.RC.OK
            elif resultado == 1:
                print('c> USERNAME IN USE')
                return client.RC.USER_ERROR
            else:
                print('c> REGISTER FAIL')
                return client.RC.ERROR

        except Exception as e:
            print('c> REGISTER FAIL')
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user - User name to unregister from the system
    # *
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist
    # * @return ERROR if another error occurred
    @staticmethod
    def unregister(usuario) :
        # Si no hay nadie conectado, o si el usuario intenta borrar una cuenta que no es la suya, error
        if not client._nombre_usuario or client._nombre_usuario != usuario:
            print('c> UNREGISTER FAIL')
            return client.RC.USER_ERROR

        # Creamos el socket TCP
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        try:
            # Conectamos el socket al servidor
            sock.connect((client._ip_servidor, client._puerto_servidor))

            # Enviamos tipo de operación
            client._sendString(sock, "UNREGISTER")

            # Enviamos el nombre de usuario
            client._sendString(sock, usuario)

            # Recibimos resultado
            resultado = client._readByte(sock)

            # Interpretamos la respuesta recibida
            if resultado == 0:
                print('c> UNREGISTER OK')
                if client._socket_recibir:
                    client._socket_recibir.close()
                client._socket_recibir = None
                client._puerto_recibir = None
                client._nombre_usuario = None

                return client.RC.OK
            elif resultado == 1:
                print('c> USER DOES NOT EXIST')
                return client.RC.USER_ERROR
            else:
                print('c> UNREGISTER FAIL')
                return client.RC.ERROR

        except Exception as e:
            print('c> UNREGISTER FAIL')
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user - User name to connect to the system
    # *
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def connect(usuario) :
        # Si el cliente está ya conectado
        if client._socket_recibir:
            print("c> USER ALREADY CONNECTED")
            return client.RC.USER_ERROR

        try:
            # Buscamos un puerto libre
            puerto = client._buscar_puerto_libre()

            # Creamos socket e hilo de escucha
            client._socket_recibir = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client._socket_recibir.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            client._socket_recibir.bind(('', puerto))
            client._socket_recibir.listen(1)
            client._puerto_recibir = puerto

            # Iniciamos hilo de escucha detached (daemon)
            if client._hilo_recibir is None or not client._hilo_recibir.is_alive():
                client._hilo_corriendo = True

                client._hilo_recibir = threading.Thread(target=client._funcion_hilo_recibir, daemon=True)
                client._hilo_recibir.start()

            # Nos conectamos servidor para enviar la solicitud
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((client._ip_servidor, client._puerto_servidor))

            # Enviamos tipo de operación
            client._sendString(sock, "CONNECT")
            # Enviamos el nombre del usuario
            client._sendString(sock, usuario)
            # Enviamos el puerto como cadena
            client._sendString(sock, str(puerto))

            # Recibimos resultado
            resultado = client._readByte(sock)
            sock.close()

            if resultado == 0:
                # Éxito
                print('c> CONNECT OK')
                client._nombre_usuario = usuario
                return client.RC.OK
            elif resultado == 1:
                # Usuario no existe
                client._socket_recibir.close()
                client._socket_recibir = None
                print('c> CONNECT FAIL, USER DOES NOT EXIST')
                return client.RC.USER_ERROR
            elif resultado == 2:
                # Ya conectado
                client._socket_recibir.close()
                client._socket_recibir = None
                print('c> USER ALREADY CONNECTED')
                return client.RC.USER_ERROR
            else:
                # Otro error
                client._socket_recibir.close()
                client._socket_recibir = None
                print('c> CONNECT FAIL')
                return client.RC.ERROR

        except Exception as e:
            # Error
            if client._socket_recibir:
                client._socket_recibir.close()
                client._socket_recibir = None
            print('c> CONNECT FAIL')
            return client.RC.ERROR

    # *
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist or if it is already connected
    # * @return ERROR if another error occurred
    @staticmethod
    def users():
        # Si el cliente no está conectado (no hay socket de escucha), error
        if not client._socket_recibir:
            print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
            return client.RC.USER_ERROR

        try:
            # Creamos el socket TCP para la consulta
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((client._ip_servidor, client._puerto_servidor))

            # Enviamos tipo de operación y nombre de usuario
            client._sendString(sock, "USERS")
            client._sendString(sock, client._nombre_usuario)

            # Recibimos el resultado (1 byte)
            resultado = client._readByte(sock)

            if resultado == 0:
                # Éxito: leer número de usuarios conectados
                num_usuarios_str = client._readString(sock)
                num_usuarios = int(num_usuarios_str)

                # Leer la lista de usuarios y actualizar el diccionario de usuarios conectados
                client._usuarios_conectados = {}
                print(f"c> CONNECTED USERS ({num_usuarios} users connected) OK")
                for i in range(num_usuarios):
                    entrada = client._readString(sock)
                    print(entrada)
                    # Parseamos "nombre :: ip :: puerto"
                    partes = entrada.split(" :: ")
                    if len(partes) == 3:
                        nombre = partes[0].strip()
                        ip = partes[1].strip()
                        puerto = int(partes[2].strip())
                        client._usuarios_conectados[nombre] = (ip, puerto)

                return client.RC.OK

            elif resultado == 1:
                # Usuario no conectado
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR

            else:
                # Otro error
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR

        except Exception as e:
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user - User name to disconnect from the system
    # *
    # * @return OK if successful
    # * @return USER_ERROR if the user does not exist
    # * @return ERROR if another error occurred
    @staticmethod
    def disconnect(usuario) :
        # Si el cliente no está conectado
        if not client._socket_recibir:
            print("c> DISCONNECT FAIL, USER NOT CONNECTED")
            return client.RC.USER_ERROR

        # Verificar que el usuario solo pueda desconectarse a sí mismo
        if usuario != client._nombre_usuario:
            print("c> DISCONNECT FAIL, USER NOT CONNECTED")
            return client.RC.USER_ERROR

        try:
            # Nos conectamos al servidor
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((client._ip_servidor, client._puerto_servidor))

            # Enviamos tipo de operación
            client._sendString(sock, "DISCONNECT")
            # Enviamos el nombre del usuario
            client._sendString(sock, usuario)

            # Recibimos resultado
            resultado = client._readByte(sock)
            sock.close()

            if resultado == 0:
                # Éxito
                client._hilo_corriendo = False
                if client._socket_recibir:
                    client._socket_recibir.close()
                    client._socket_recibir = None
                if client._hilo_recibir is not None:
                    client._hilo_recibir.join()
                    client._hilo_recibir = None
                client._puerto_recibir = None
                client._nombre_usuario = None
                client._usuarios_conectados = {}

                print('c> DISCONNECT OK')
                return client.RC.OK
            elif resultado == 1:
                # Usuario no existe
                print('c> DISCONNECT FAIL, USER DOES NOT EXIST')
                return client.RC.USER_ERROR
            elif resultado == 2:
                # No está conectado
                print('c> DISCONNECT FAIL, USER NOT CONNECTED')
                return client.RC.USER_ERROR
            else:
                # Otro error
                print('c> DISCONNECT FAIL')
                return client.RC.ERROR

        except Exception as e:
            # Error
            client._socket_recibir = None
            print('c> DISCONNECT FAIL')
            return client.RC.ERROR

    # *
    # * @param user    - Receiver user name
    # * @param message - Message to be sent
    # *
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def send(usuario, mensaje):

        # Si el cliente no está conectado
        if not client._socket_recibir:
            print("c> SEND FAIL")
            return client.RC.USER_ERROR

        # Creamos el socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Nos conectamos al servicio web local
        wsdl_url = "http://localhost:8000/?wsdl"
        soap_cliente = zeep.Client(wsdl=wsdl_url)

        # Formateamos el mensaje
        mensaje_formateado = soap_cliente.service.convertir(mensaje)

        # Comprobamos que el mensaje tiene 255 caracteres o menos
        if len(mensaje_formateado) > 255:
            print('c> SEND FAIL')
            return client.RC.ERROR

        # Conexión y envío de la petición
        try:
            sock.connect((client._ip_servidor, client._puerto_servidor))
            client._sendString(sock, "SEND")
            # Envío de datos
            client._sendString(sock, client._nombre_usuario)
            client._sendString(sock, usuario)
            client._sendString(sock, mensaje_formateado)

            # Manejo de la respuesta
            resultado = client._readByte(sock)

            if resultado == 0:
                id_mensaje = client._readString(sock)
                print(f'c> SEND OK - MESSAGE {id_mensaje}')
                return client.RC.OK
            elif resultado == 1:
                print('c> SEND FAIL, USER DOES NOT EXIST')
                return client.RC.USER_ERROR
            else:
                print('c> SEND FAIL')
                return client.RC.ERROR

        # Manejo de errores y cierre
        except Exception as e:
            print('c> SEND FAIL')
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param user    - Receiver user name
    # * @param fichero - Fichero a enviar
    # * @param mensaje - Message to be sent
    # *
    # * @return OK if the server had successfully delivered the message
    # * @return USER_ERROR if the user is not connected (the message is queued for delivery)
    # * @return ERROR the user does not exist or another error occurred
    @staticmethod
    def sendAttach(usuario, fichero, mensaje):

        # Si el cliente no está conectado
        if not client._socket_recibir:
            print("c> SENDATTACH FAIL")
            return client.RC.USER_ERROR

        # Creamos el socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        # Nos conectamos al servicio web local
        wsdl_url = "http://localhost:8000/?wsdl"
        soap_cliente = zeep.Client(wsdl=wsdl_url)

        # Formateamos el mensaje
        mensaje_formateado = soap_cliente.service.convertir(mensaje)

        # Comprobamos que el mensaje tiene 255 caracteres o menos
        if len(mensaje_formateado) > 255:
            print('c> SENDATTACH FAIL')
            return client.RC.ERROR

        # Conexión y envío de la petición
        try:
            sock.connect((client._ip_servidor, client._puerto_servidor))
            client._sendString(sock, "SENDATTACH")
            client._sendString(sock, client._nombre_usuario)
            client._sendString(sock, usuario)
            client._sendString(sock, mensaje_formateado)
            client._sendString(sock, fichero)

            # Manejo de la respuesta
            resultado = client._readByte(sock)

            if resultado == 0:
                id_mensaje = client._readString(sock)
                print(f'c> SENDATTACH OK - MESSAGE {id_mensaje}')
                return client.RC.OK
            elif resultado == 1:
                print('c> SENDATTACH FAIL, USER DOES NOT EXIST')
                return client.RC.USER_ERROR
            else:
                print('c> SENDATTACH FAIL')
                return client.RC.ERROR

        except Exception as e:
            print('c> SENDATTACH FAIL')
            return client.RC.ERROR
        finally:
            sock.close()

    # *
    # * @param usuario       - Usuario que tiene el fichero
    # * @param fichero       - Nombre del fichero remoto a transferir
    # * @param fichero_local - Nombre del fichero local donde guardar el contenido
    # *
    # * @return OK si la transferencia fue exitosa
    # * @return ERROR si hubo algún problema
    @staticmethod
    def getFile(usuario, fichero, fichero_local):

        # Si el usuario no está en el diccionario, refrescamos con USERS
        if usuario not in client._usuarios_conectados:
            client.users()

        # Comprobamos de nuevo tras el refresco
        if usuario not in client._usuarios_conectados:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.ERROR

        ip_destino, puerto_destino = client._usuarios_conectados[usuario]

        try:
            # Conectamos directamente al hilo de escucha del otro cliente
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((ip_destino, puerto_destino))

            # Enviamos operación, nombre de usuario solicitante y nombre del fichero
            client._sendString(sock, "GET_FILE")
            client._sendString(sock, client._nombre_usuario)
            client._sendString(sock, fichero)

            # Recibimos el contenido del fichero y lo guardamos localmente
            with open(fichero_local, "wb") as f:
                while True:
                    datos = sock.recv(4096)
                    if not datos:
                        break
                    f.write(datos)

            sock.close()
            print(f"c> FILE TRANSFER OK")
            return client.RC.OK

        except Exception as e:
            print("c> FILE TRANSFER FAILED, user not connected.")
            return client.RC.ERROR

    # *
    # * @brief Command interpreter for the client. It calls the protocol functions.
    @staticmethod
    def shell():

        while (True) :
            try :
                command = input("c> ")
                line = command.split(" ")
                if (len(line) > 0):

                    line[0] = line[0].upper()

                    if (line[0]=="REGISTER") :
                        if (len(line) == 2) :
                            client.register(line[1])
                        else :
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif(line[0]=="UNREGISTER") :
                        if (len(line) == 2) :
                            client.unregister(line[1])
                        else :
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif(line[0]=="CONNECT") :
                        if (len(line) == 2) :
                            client.connect(line[1])
                        else :
                            print("Syntax error. Usage: CONNECT <userName>")

                    elif(line[0]=="DISCONNECT") :
                        if (len(line) == 2) :
                            client.disconnect(line[1])
                        else :
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif(line[0]=="USERS") :
                        if (len(line) == 1) :
                            client.users()
                        else :
                            print("Syntax error. Usage: CONNECTED_USERS <userName>")

                    elif(line[0]=="SEND") :
                        if (len(line) >= 3) :
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else :
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif(line[0]=="SENDATTACH") :
                        if (len(line) >= 4) :
                            fichero = line[-1]
                            message = ' '.join(line[2:-1])
                            client.sendAttach(line[1], fichero, message)
                        else :
                            print("Syntax error. Usage: SENDATTACH <userName> <message> <filename>")

                    elif(line[0]=="GETFILE") :
                        if (len(line) == 4) :
                            client.getFile(line[1], line[2], line[3])
                        else :
                            print("Syntax error. Usage: GETFILE <userName> <fileName> <localFileName>")

                    elif(line[0]=="QUIT") :
                        if (len(line) == 1) :
                            break
                        else :
                            print("Syntax error. Use: QUIT")
                    else :
                        print("Error: command " + line[0] + " not valid.")
            except Exception as e:
                print("Exception: " + str(e))

    # *
    # * @brief Prints program usage
    @staticmethod
    def usage() :
        print("Usage: python3 client.py -s <server> -p <port>")

    # *
    # * @brief Parses program execution arguments
    @staticmethod
    def parseArguments(argv) :
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if (args.s is None):
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if ((args.p < 1024) or (args.p > 65535)):
            parser.error("Error: Port must be in the range 1024 <= port <= 65535")
            return False

        client._ip_servidor = args.s
        client._puerto_servidor = args.p

        return True

    # ******************** MAIN *********************
    @staticmethod
    def main(argv) :
        if (not client.parseArguments(argv)) :
            client.usage()
            return

        client.shell()
        print("+++ FINISHED +++")


if __name__=="__main__":
    client.main([])
