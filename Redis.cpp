#include "Redis.h"

/**
 * Check if the response is an error.
 * @return Return the error message or "".
 */
String Redis::checkError(String resp)
{
    if (resp.startsWith("-"))
    {
        return resp.substring(1, resp.indexOf("\r\n"));
    }
    return "";
}

#include <vector>
#include <memory>

#define CRLF F("\r\n")

class RedisType {
public:
    RedisType(char tc) : type_char(tc) {}
    static std::unique_ptr<RedisType> parseType(String&);
    virtual operator String() = 0;
// I'm thinking a 'char typeChar' will be needed here...
protected:
    char type_char;
};

// need a 'class RedisConcreteType : public RedisType' that implements a shared common ctor()?

class RedisSimpleString : public RedisType {
public:
    RedisSimpleString(String d) : data(d), RedisType('+') {}

    virtual operator String() override
    {
        String emitStr(type_char);
        // Simple strings cannot contain CRLF, as they must terminate with CRLF
        // https://redis.io/topics/protocol#resp-simple-strings
        data.replace("\r\n", "");
        emitStr += data;
        emitStr += CRLF;
        return emitStr;
    }

protected:
    String data;
};

class RedisBulkString : public RedisType {
public:
    RedisBulkString(String d) : data(d), RedisType('$') {}
    virtual operator String() override
    {
        String emitStr(type_char);
        emitStr += String(data.length());
        emitStr += CRLF;
        emitStr += data;
        emitStr += CRLF;
        return emitStr;
    }

protected:
    String data;
};

class RedisArray : public RedisType {
public:
    RedisArray() : RedisType('*') {}

    void add(RedisType* param) {
        vec.push_back(param);
    }

    virtual operator String() override {
        String emitStr(type_char);
        emitStr += String(vec.size());
        emitStr += CRLF;
        for (auto rTypeInst : vec) {
            emitStr += *rTypeInst;
        }
        return emitStr;
    }

protected:
    std::vector<RedisType*> vec;
};

class RedisError : public RedisSimpleString {
public:
    RedisError(String d) : RedisSimpleString(d) {
        type_char = '-';
    }
};

class RedisCommand : public RedisArray {
public:
    RedisCommand(String command) : _cmd(command) {}

    /*
    RedisType issue(const Stream& cmdStream) {

    }
    */
private:
    String _cmd;
};

class RedisAUTH : public RedisCommand {
public:
    RedisAUTH(String password) : RedisCommand("AUTH") {
    }
};

std::unique_ptr<RedisType> RedisType::parseType(String& data)
{
    RedisType *rv = nullptr;
   
    if (data.length()) {
        switch (data.charAt(0)) {
            case '+': 
                rv = new RedisSimpleString(data); 
                break;
            case '-': 
            default:
                rv = new RedisError(data); 
                break;
        }
    }

    return std::unique_ptr<RedisType>(rv);
}


/**
 * Open the Redis connection.
 */
RedisReturnValue Redis::connect(const char* password)
{
    if(conn.connect(addr, port)) 
    {
        // the NoDelay and Timeout should be specified prior to making the connection
        conn.setNoDelay(noDelay);
        conn.setTimeout(timeout);
        int passwordLength = strlen(password);
        if (passwordLength > 0)
        {
            RedisBulkString auth("AUTH"), pass(password);
            RedisArray authArr;
            //authArr.vec.push_back(std::shared_ptr<RedisType>(&auth));
            //authArr.vec.push_back(std::shared_ptr<RedisType>(&pass));
            authArr.add(&auth);
            authArr.add(&pass);
            auto respStr = (String)authArr;
            Serial.printf("RESPSTR:\n%s\n", respStr.c_str());
            conn.write(respStr.c_str());
            
            int c = 0;
            while (!conn.available() && c++ < 100) {
                delay(10);
            }

            String resp = conn.readStringUntil('\0');
            return resp.indexOf("+OK") == -1 ? RedisAuthFailure : RedisSuccess;
        }
        return RedisSuccess;
    }
    return RedisConnectFailure;
}

/**
 * Process the SET command (see https://redis.io/commands/set).
 * @param key The key.
 * @param value The assigned value.
 * @return If it's okay.
 */
bool Redis::set(const char* key, const char* value)
{
    conn.println("*3");
    conn.println("$3");
    conn.println("SET");
    conn.print("$");
    conn.println(strlen(key));
    conn.println(key);
    conn.print("$");
    conn.println(strlen(value));
    conn.println(value);

    String resp = conn.readStringUntil('\0');
    return resp.indexOf("+OK") != -1;
}

/**
 * Process the GET command (see https://redis.io/commands/get).
 * @param key The key.
 * @return Found value.
 */

String Redis::get(const char* key) 
{
    conn.println("*2");
    conn.println("$3");
    conn.println("GET");
    conn.print("$");
    conn.println(strlen(key));
    conn.println(key);

    String resp = conn.readStringUntil('\0');
    String error = checkError(resp);
    if (error != "")
    {
        return error;
    }
    if (resp.startsWith("$-1"))
    {
        return "";
    }
    int start = resp.indexOf("\r\n");
    int length = resp.substring(1, start).toInt();
    return resp.substring(start + 2, start + length + 2);
}

/**
 * Process the PUBLISH command (see https://redis.io/commands/publish).
 * @param key The channel.
 * @param value The message.
 * @return Number of subscribers which listen this message.
 */
int Redis::publish(const char* channel, const char* message)
{
    conn.println("*3");
    conn.println("$7");
    conn.println("PUBLISH");
    conn.print("$");
    conn.println(strlen(channel));
    conn.println(channel);
    conn.print("$");
    conn.println(strlen(message));
    conn.println(message);

    String resp = conn.readStringUntil('\0');
    if (checkError(resp) != "")
    {
        return -1;
    }
    return resp.substring(1, resp.indexOf("\r\n")).toInt();
}

/**
 * Close the Redis connection.
 */
void Redis::close(void)
{
    conn.stop();
}
