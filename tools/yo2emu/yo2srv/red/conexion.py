"""Conn: una conexión del cliente (paso E1b de la reestructura, REESTRUCTURA.md §2.2 y §3.4).

Es la clase de run.py de siempre, SIN cambiar la lógica: el cliente exige que los mensajes que recibe vayan numerados
1, 2, 3... (campo «parámetro» de la cabecera) y procesa uno por fotograma, así que se mandan en cola y con pausa. La
numeración sobrevive a las reconexiones de un mismo cliente que siguen su cuenta en `Conn.GATE_SEQ` (el MISMO dict
que antes: lo usan los hot_*.py y las pruebas por `Conn.GATE_SEQ`), por cliente y no por IP (26-10: ver `seq`).

Lo que cambia es de dónde sale cada nombre externo (regla de oro de §2.3): `log`, `current_conn` (la reasignan las
cargas en caliente) y `_duel_ticket_issue` (la reserva de duelo, de run.py) se leen SIEMPRE como `srv.X` en el momento de
usarlos, es decir, del namespace de run.py; no hay copias en este módulo. run.py importa de aquí el MISMO nombre `Conn`.
Los métodos son los mismos objetos que antes: los hot_*.py que cambian `Conn.send` o `Conn.saw_client` por `__code__`
siguen funcionando (research/seguridad_duelo/hot_reserva.py y research/cuentas/hot_en_uso.py).
"""
import queue
import threading
import time

from . import proto

srv = None      # el Server; lo fija setup(). Hasta entonces nada de este módulo se puede llamar.


def setup(servidor):
    """Enlaza el módulo con el Server (idempotente). run.py lo llama (yo2srv.red.setup) antes de importar `Conn`."""
    global srv
    srv = servidor
    return servidor


class Conn:
    """Una conexión del cliente. El cliente exige que los mensajes que recibe vayan numerados
    1, 2, 3... (campo «parámetro» de la cabecera) y procesa uno por fotograma: se mandan en cola,
    con pausa entre ellos."""
    # V01 (P0-1): el cliente comprueba la numeración POR CONEXIÓN (FUN_0068bac0: param_1[1]++ y 0xffffff35 si el
    # número no es contador + 1). En el log, toda conexión de cuentas, lobby, sala o duelo empieza en #1; solo la
    # puerta (5730) sigue la cuenta entre conexiones del mismo cliente (#3, #5... en la 2.ª y 3.ª). Antes era un
    # contador por PUERTO compartido por todos: con dos clientes, cada envío a uno desfasaba al otro para siempre.
    # Ahora: contador propio de cada conexión, y la que llega con el número del cliente seguido continúa la de ese
    # cliente (GATE_SEQ, ver `seq`). Se reinicia cuando el cliente vuelve a empezar desde #1.
    GATE_SEQ = {}
    MAX_DATA = 1024   # B04: FUN_00689ea0 -> si el largo > 0x400, estado 0xff y el flujo queda desalineado para siempre
    def __init__(self, sock, port):
        self.sock, self.port = sock, port
        self.lock = threading.Lock()
        self.closed = False
        self._seq = 0                          # número del último mensaje mandado por ESTA conexión
        self._last = self._pend = self._kept = None   # último número del cliente; cuenta a seguir al saber la cuenta
        self.sess = None                       # sesión de la cuenta (0x3003; en duelo, el pid de 0x6001)
        self._outq = None                      # cola de envío (post), se crea con el primer uso
        self._qlock = threading.Lock()         # no self.lock: send lo tiene cogido mientras duerme su pausa
        try:
            self.peer = sock.getpeername()[0]
        except (OSError, IndexError):          # IndexError: socket sin IP de par (socketpair AF_UNIX de las pruebas)
            self.peer = "?"
    # 2026-10-06, en vivo: el 2.º duelo se colgaba. La conexión de duelo NUEVA llega con el número del cliente seguido
    # (0x6001 #25) y espera que el servidor también siga su cuenta en ese puerto; con el contador por conexión le
    # llegaba #1 y lo tiraba (FUN_0068bac0). Igual en la puerta, y en cuentas al volver a entrar desde el título en el
    # MISMO proceso (0x3001 #29, 27-10). Regla, en todos los puertos: una conexión que empieza en #1 lleva su contador
    # propio (self._seq); una que empieza más adelante sigue la cuenta que el cliente dejó en ese puerto.
    # 26-10 (web): antes el contador de TODOS los puertos era uno por (IP, puerto). En la web pública todos los jugadores
    # llegan por la pasarela desde 127.0.0.1: compartían número, y cada aviso a uno (0x3112, 0x4181) desfasaba al otro,
    # que tiraba en silencio lo siguiente (el que entraba en un bloque ocupado se quedaba en My Room). Ahora la cuenta
    # que sobrevive va por (cuenta o IP, puerto, número del siguiente mensaje del cliente): dos clientes tras la misma IP
    # no se pisan. La clave (cuenta o IP, puerto) queda como respaldo (lo de antes) si no hay otra.
    def _who(self):
        return getattr(getattr(self, "sess", None), "acct", None) or self.peer
    @property
    def seq(self):
        n = getattr(self, "_pend", None)
        if n is not None and getattr(getattr(self, "sess", None), "acct", None):
            self._pend = None                    # duelo: la cuenta ya se sabe (0x6001 -> attach): su cuenta seguida
            self._seq = Conn.GATE_SEQ.get((self._who(), self.port, n), Conn.GATE_SEQ.get((self._who(), self.port), 0))
        return self._seq
    @seq.setter
    def seq(self, v):
        self._seq = v
        self._keep()
    def _keep(self):
        last = getattr(self, "_last", None)
        if last is None:
            return
        # por la cuenta y también por la IP: la 1.ª conexión de cuentas o lobby al volver a entrar aún no tiene sesión
        keys = {(w, self.port, last + 1) for w in (self._who(), self.peer)}
        for k in (getattr(self, "_kept", None) or set()) - keys:
            Conn.GATE_SEQ.pop(k, None)
        for w in (self._who(), self.peer):
            Conn.GATE_SEQ[(w, self.port, last + 1)] = Conn.GATE_SEQ[(w, self.port)] = self._seq
        self._kept = keys
    def saw_client(self, param):
        # 07-10 (−242, research/cuentas/NOTES.md §6): hora del último mensaje del cliente por esta conexión. Lo llama
        # _handle con CADA mensaje (también los lectores de antes de una carga en caliente: es un método de la clase).
        self.rx_t = time.time()
        if param == 1:
            self._seq, self._pend = 0, None
        elif getattr(self, "_last", None) is None:   # 1.er mensaje y no es #1: sigue la cuenta del cliente
            if self.port == getattr(srv, "DUEL_PORT", None) and not getattr(getattr(self, "sess", None), "acct", None):
                self._pend = param               # la cuenta llega con el 0x6001 (attach): se mira al contestar
            else:
                self._seq = Conn.GATE_SEQ.get((self._who(), self.port, param), Conn.GATE_SEQ.get((self._who(), self.port), 0))
        self._last = param
        self._keep()
    def stale(self):
        """True si ya no se debe mandar por aquí: cerrada, o ESTE cliente ya usa otra conexión de este puerto (su
        sesión, o su IP mientras no tiene sesión). Antes miraba CONNS[puerto]: con dos clientes, la conexión del
        primero quedaba «vieja» en cuanto el segundo entraba en el mismo puerto."""
        if self.closed:
            return True
        cur = srv.current_conn(self)
        if cur is None or cur is self:
            return False
        # 13-10: una conexión que aún no ha dicho nada no deja vieja a otra que sí habla. El lanzador del anfitrión
        # comprueba la puerta cada 10 s abriendo y cerrando una conexión desde 127.0.0.1, la misma IP que su propio
        # juego: si caía justo tras el 0x2005 del juego, la lista de servidores (0x2003) se descartaba y el juego se
        # quedaba en la pantalla de carga. Mandar por la buena mientras la nueva calla no rompe nada (B59: el cliente
        # solo tira lo que llega por un socket que ya no usa).
        return getattr(cur, "rx_t", None) is not None or getattr(self, "rx_t", None) is None
    def post(self, msg_id, data=b"", pause=0.02):
        """V10 (P1-10): encola y vuelve al momento; un hilo por conexión manda en orden con su pausa. Para avisos a
        OTROS clientes (posición, entradas y salidas del bloque): send duerme `pause` en el hilo del que llama, y el
        hilo que lee al que camina se quedaba parado pausa × receptores."""
        if self.stale():
            return
        with self._qlock:
            if self._outq is None:
                self._outq = queue.Queue()
                threading.Thread(target=self._sender, daemon=True, name=f"post-{self.port}-{id(self) & 0xffff:04x}").start()
        self._outq.put((msg_id, bytes(data), pause))
    def _sender(self):
        while True:
            item = self._outq.get()
            if item is None:
                return
            try:
                self.send(*item)
            except Exception as e:                # socket caído: se deja de mandar por aquí
                srv.log(f"[srv:{self.port}] post 0x{item[0]:04x}: {e!r}")
                return
    def stop_post(self):
        if self._outq is not None:
            self._outq.put(None)
    def send(self, msg_id, data=b"", pause=0.25):
        # B59: no se manda por una conexión vieja o cerrada, y el número solo se gasta si sendall sale bien
        # (FUN_0068bac0 tira en silencio todo lo que no lleva el seq exacto). Desde V01 el número es de la conexión.
        if len(data) > Conn.MAX_DATA:
            srv.log(f"[ERROR] [srv:{self.port}] 0x{msg_id:04x} de {len(data)} B > {Conn.MAX_DATA}: NO se manda "
                    f"(rompería el flujo del cliente)")
            return
        if self.stale():
            srv.log(f"[aviso] [srv:{self.port}] 0x{msg_id:04x} por una conexión {'cerrada' if self.closed else 'vieja'}: descartado")
            return
        if msg_id == 0x4306:                     # seguridad_duelo (07-10): reserva para el 0x6001, ANTES de mandarlo
            srv._duel_ticket_issue(self)         # (todos los caminos: NPC, sala, reto, autoduelo, torneo). Nunca lanza
        with self.lock:
            seq = self.seq + 1
            try:
                self.sock.sendall(proto.pack(msg_id, data, seq))
            except OSError as e:
                self.closed = True
                srv.log(f"[srv:{self.port}] -> 0x{msg_id:04x} #{seq} FALLA ({e!r}): conexión cerrada, número sin gastar")
                raise
            self.seq = seq
            srv.log(f"[srv:{self.port}] -> 0x{msg_id:04x} #{seq} len={len(data)} {data[:24].hex(' ')}")
            time.sleep(pause)
