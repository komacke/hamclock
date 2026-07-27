/* implement WiFiClient using normal UNIX sockets
 */

#include <signal.h>

#include "IPAddress.h"
#include "WiFiClient.h"

// default constructor
WiFiClient::WiFiClient()
{
        // init
    socket = -1;
    n_peek = 0;
    next_peek = 0;
    read_pending_ms = get_timeout_ms();
    m_isPipe = false;
    m_pipe = nullptr;
}

// constructor handed an open socket to use
WiFiClient::WiFiClient(int fd)
{
    if (fd >= 0 && debugLevel (DEBUG_NET, 1))
        printf ("WiFiCl: new WiFiClient inheriting fd %d\n", fd);

    // init
    socket = fd;
    n_peek = 0;
    next_peek = 0;
    m_isPipe = false;
    m_pipe = nullptr;
}

// return whether this socket is active
WiFiClient::operator bool()
{
    bool active = socket >= 0;
    if (active && debugLevel (DEBUG_NET, 2))
        printf ("WiFiCl: fd %d is active\n", socket);
    return (active);
}

int WiFiClient::connect_to (int sockfd, struct sockaddr *serv_addr, int addrlen, int to_ms)
{
    unsigned int len;
    int err;
    int flags;
    int ret;

    /* set socket non-blocking */
    flags = fcntl (sockfd, F_GETFL, 0);
    (void) fcntl (sockfd, F_SETFL, flags | O_NONBLOCK);

    /* start the connect */
    ret = ::connect (sockfd, serv_addr, addrlen);
    if (ret < 0 && errno != EINPROGRESS)
        return (-1);

    /* wait for sockfd to become useable */
    ret = tout (to_ms, sockfd);
    if (ret < 0)
        return (-1);

    /* verify connection really completed */
    len = sizeof(err);
    err = 0;
    ret = getsockopt (sockfd, SOL_SOCKET, SO_ERROR, (char *) &err, &len);
    if (ret < 0)
        return (-1);
    if (err != 0) {
        errno = err;
        return (-1);
    }

    /* looks good - restore blocking */
    if (fcntl (sockfd, F_SETFL, flags) < 0)
        printf ("WiFiCl: fcntl fd %d: %s\n", sockfd, strerror(errno));

    return (0);
}

int WiFiClient::tout (int to_ms, int fd)
{
    fd_set rset, wset;
    struct timeval tv;
    int ret;

    FD_ZERO (&rset);
    FD_ZERO (&wset);
    FD_SET (fd, &rset);
    FD_SET (fd, &wset);

    tv.tv_sec = to_ms / 1000;
    tv.tv_usec = to_ms % 1000;

    ret = select (fd + 1, &rset, &wset, NULL, &tv);
    if (ret > 0)
        return (0);
    if (ret == 0)
        errno = ETIMEDOUT;
    return (-1);
}

/* Add this new method to WiFiClient (e.g. in the .cpp file) */
/* Call it instead of connect() when you want to run curl via a pipe */
bool WiFiClient::connectCommand(const char* command)
{
    /* clean up any previous pipe */
    if (m_pipe) {
        pclose(m_pipe);
        m_pipe = nullptr;
    }
    m_isPipe = false;

    m_pipe = popen(command, "r");
    if (!m_pipe) {
        return false;
    }

    socket = fileno(m_pipe);   /* reuse the same "socket" member as the file descriptor */
    if (socket < 0) {
        pclose(m_pipe);
        m_pipe = nullptr;
        return false;
    }

    m_isPipe = true;
    n_peek = 0;
    next_peek = 0;

    return true;
}
bool WiFiClient::connect(const char *host, int port)
{
    struct addrinfo hints, *aip;
    char port_str[16];
    int sockfd;
    /* connect is not a command pipe so clear pipe information */
    m_isPipe = false;
    m_pipe = nullptr;
    /* lookup host address.
     * N.B. must call freeaddrinfo(aip) after successful call before returning
     */
    memset (&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf (port_str, sizeof(port_str), "%d", port);
    int error = ::getaddrinfo (host, port_str, &hints, &aip);
    if (error) {
        printf ("WiFiCl: getaddrinfo(%s:%d): %s\n", host, port, gai_strerror(error));
        return (false);
    }

    /* create socket */
    sockfd = ::socket (aip->ai_family, aip->ai_socktype, aip->ai_protocol);
    if (sockfd < 0) {
        freeaddrinfo (aip);
        printf ("WiFiCl: socket(%s:%d): %s\n", host, port, strerror(errno));
        return (false);
    }

    /* connect */
    if (connect_to (sockfd, aip->ai_addr, aip->ai_addrlen, 8000) < 0) {
        printf ("WiFiCl: connect(%s:%d): %s\n", host, port, strerror(errno));
        freeaddrinfo (aip);
        close (sockfd);
        return (false);
    }

    /* handle write errors inline */
    signal (SIGPIPE, SIG_IGN);

    /* ok start fresh */
    if (debugLevel (DEBUG_NET, 1))
        printf ("WiFiCl: new %s:%d fd %d\n", host, port, sockfd);
    freeaddrinfo (aip);

    // init much like constructors
    socket = sockfd;
    n_peek = 0;
    next_peek = 0;

    return (true);
}

bool WiFiClient::connect(IPAddress ip, int port)
{
    char host[32];
    snprintf (host, sizeof(host), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    return (connect (host, port));
}

void WiFiClient::setNoDelay(bool on)
{
    // control Nagle algorithm
    socklen_t flag = on;
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (void *) &flag, sizeof(flag)) < 0)
        printf ("WiFiCl: TCP_NODELAY(%d): %s\n", on, strerror(errno));     // not fatal
}

void WiFiClient::stop()
{
    if (m_isPipe) {
        if (m_pipe != nullptr) {
            pclose(m_pipe);
            m_pipe = nullptr;
        }
        m_isPipe = false;
    } else if (socket >= 0) {
              if (debugLevel (DEBUG_NET, 1))
                printf ("WiFiCl: stopping fd %d\n", socket);
        shutdown (socket, SHUT_RDWR);
        close (socket);
        socket = -1;

    } else if (debugLevel (DEBUG_NET, 2))
            printf ("WiFiCl: fd %d already stopped\n", socket);
    n_peek = 0;
    next_peek = 0;
}

bool WiFiClient::connected()
{
    return (socket >= 0);
}

/* return whether more is available after waiting up to ms.
 * non-standard
 */
bool WiFiClient::pending(int ms)
{
    struct timeval tv;
    fd_set rset;
    FD_ZERO (&rset);
    FD_SET (socket, &rset);
    tv.tv_sec = ms/1000;
    tv.tv_usec = (ms-1000*tv.tv_sec)*1000;
    int s = select (socket+1, &rset, NULL, NULL, &tv);
    if (s < 0) {
        printf ("WiFiCl: fd %d select(%d ms): %s\n", socket, ms, strerror(errno));
    stop();
    return (false);
}

    bool more = s > 0;

    if (debugLevel (DEBUG_NET, 2))
        printf ("WiFiCl: %smore pending\n", more ? "" : "no ");

    return (more);
}

/* return next value in peek or wait up to given time to add more.
 * return 1 if read() or readArray() will return immediately else 0 if nothing more.
 */
int WiFiClient::available (int pending_ms)
{
    // certainly none if closed
    if (socket < 0)
        return (0);

    // simple if unread bytes already available
    if (next_peek < n_peek)
        return (1);

    // wait as instructed
    if (!pending(pending_ms))
        return (0);

    // read more
    int nr = ::read(socket, peek, sizeof(peek));
    if (nr > 0) {
        if (debugLevel (DEBUG_NET, 2))
            printf ("WiFiCl: available read(%d,%ld) %d\n", socket, (long)sizeof(peek), nr);
        if (debugLevel (DEBUG_NET, 3))
            logBuffer (peek, nr);
        n_peek = nr;
        next_peek = 0;
        return (1);
    } else if (nr == 0) {
        if (debugLevel (DEBUG_NET, 1))
            printf ("WiFiCl: available read(%d) EOF\n", socket);
        stop();
        return (0);
    } else {
        if (debugLevel (DEBUG_NET, 1))
            printf ("WiFiCl: available read(%d): %s\n", socket, strerror(errno));
        stop();
        return (0);
    }
}

/* wait as long as READ_PENDING_MS read next char.
 * return char else -1 if EOF
 */
int WiFiClient::read()
{
    if (available (read_pending_ms)) {
        uint8_t p = peek[next_peek++];
        if (debugLevel (DEBUG_NET, 3)) {
            int n_more = n_peek - next_peek;
            if (isprint (p))
                printf ("WiFiCl: read(%d) returning %c %d, %d more\n", socket, p, p, n_more);
            else
                printf ("WiFiCl: read(%d) returning   %d, %d more\n", socket, p, n_more);
        }
        return (p);
    }
    return (-1);
}

/* wait as long as read_pending_ms to read up to count more bytes into array.
 * return actual count or 0 when no more.
 */
int WiFiClient::readArray (uint8_t *array, long count)
{
    int n_return = 0;

    if (available (read_pending_ms)) {
        int n_available = n_peek - next_peek;
        n_return = count > n_available ? n_available : count;
        memcpy (array, &peek[next_peek], n_return);
        next_peek += n_return;
    }

    if (debugLevel (DEBUG_NET, 2))
        printf ("WiFiCl: readArray(%d,%ld) %d\n", socket, count, n_return);
    return (n_return);
}

int WiFiClient::write (const uint8_t *buf, int n)
{
    // can't if closed
    if (socket < 0)
        return (0);

    int nw = 0;
    for (int ntot = 0; ntot < n; ntot += nw) {
        nw = ::write (socket, buf+ntot, n-ntot);
        if (nw < 0) {
            // select says it won't block but it still might be temporarily EAGAIN
            if (errno != EAGAIN) {
                printf ("WiFiCl: write(%d) after %d: %s\n", socket, ntot, strerror(errno));
                stop();             // avoid repeated failed attempts
                return (0);
            } else
                nw = 0;             // act like nothing happened
        } else if (nw == 0) {
            printf ("WiFiCl: write(%d) returns 0 after %d\n", socket, ntot);
            stop();             // avoid repeated failed attempts
            return (0);
        }
    }

    if (debugLevel (DEBUG_NET, 2))
        printf ("WiFiCl: write(%d) %d\n", socket, n);
    if (debugLevel (DEBUG_NET, 3))
        logBuffer (buf, n);

    return (n);
}

/* log the given buffer contents
 */
void WiFiClient::logBuffer (const uint8_t *buf, int nbuf)
{
    const int max_pg_w = 100;
    int pg_w = 0;
    bool last_nl = false;
    while (nbuf-- > 0) {
        uint8_t c = *buf++;
        if (isprint(c)) {
            pg_w += printf ("%c", c);
            last_nl = false;
        } else {
            pg_w += printf (" %02X", c);
            last_nl = c == '\n';
        }
        if (pg_w > max_pg_w) {
            printf ("\n");
            pg_w = 0;
            last_nl = true;
        }
    }
    if (!last_nl)
        printf ("\n");
}

void WiFiClient::print (void)
{
}

void WiFiClient::print (String s)
{
    const uint8_t *sp = (const uint8_t *) s.c_str();
    int n = strlen ((char*)sp);
    write (sp, n);
}

void WiFiClient::print (const char *str)
{
    write ((const uint8_t *) str, strlen(str));
}

void WiFiClient::print (float f)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%g", f);
    write ((const uint8_t *) buf, n);
}

void WiFiClient::print (float f, int s)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%.*f", s, f);
    write ((const uint8_t *) buf, n);
}

void WiFiClient::println (void)
{
    write ((const uint8_t *) "\r\n", 2);
}

void WiFiClient::println (String s)
{
    const uint8_t *sp = (const uint8_t *) s.c_str();
    int n = strlen ((char*)sp);
    write (sp, n);
    write ((const uint8_t *) "\r\n", 2);
}

void WiFiClient::println (const char *str)
{
    write ((const uint8_t *) str, strlen(str));
    write ((const uint8_t *) "\r\n", 2);
}

void WiFiClient::println (float f)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%g\r\n", f);
    write ((const uint8_t *) buf, n);
}

void WiFiClient::println (float f, int s)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%.*f\r\n", s, f);
    write ((const uint8_t *) buf, n);
}

void WiFiClient::println (int i)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%d\r\n", i);
    write ((const uint8_t *) buf, n);
}

void WiFiClient::println (uint32_t i)
{
    char buf[32];
    int n = snprintf (buf, sizeof(buf), "%u\r\n", i);
    write ((const uint8_t *) buf, n);
}

IPAddress WiFiClient::remoteIP()
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);

    getpeername(socket, (struct sockaddr *)&sa, &len);
    struct in_addr ip_addr = sa.sin_addr;

    char *s = inet_ntoa (ip_addr);
    int oct0, oct1, oct2, oct3;
    sscanf (s, "%d.%d.%d.%d", &oct0, &oct1, &oct2, &oct3);
    return (IPAddress(oct0,oct1,oct2,oct3));
}
