struct Log {
    string usuario<256>;
    string operacion<256>;
    string fichero<512>;
};

program LOG {
    version LOG_VER {
        int IMPRIMIR_LOG(struct Log) = 1;
    } = 1;
} = 99;