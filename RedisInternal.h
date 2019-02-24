#ifndef REDIS_INTERNAL_H
#define REDIS_INTERNAL_H

#include "Arduino.h"
#include "Client.h"
#include <vector>
#include <memory>
#include <functional>

#define CRLF F("\r\n")
#define ARDUINO_REDIS_SERIAL_TRACE 0

typedef std::vector<String> ArgList;

/** A basic object model for the Redis serialization protocol (RESP):
 *      https://redis.io/topics/protocol
 */

/* The lack of RTTI on Ardunio is unfortunate but completely understandable. 
 * However, we're not going to let that stop us, so RedisObject implements a basic 
 * but functional type system for Redis objects. 
 */
class RedisObject {
public:
    /** Denote a basic Redis type, with NoType and InternalError defined specifically for this API */
    typedef enum {
        NoType = '\0',
        SimpleString = '+',
        Error = '-',
        Integer = ':',
        BulkString = '$',
        Array = '*',
        InternalError = '!'
    } Type;

    RedisObject() {}
    RedisObject(Type tc) : _type(tc) {}
    RedisObject(Type tc, Client& c) : _type(tc) { init(c); }
    
    ~RedisObject() {}

    static std::shared_ptr<RedisObject> parseType(Client&);

    /** Initialize a RedisObject instance from the bytestream represented by 'client'.
     *  Only does very basic (e.g. SimpleString-style) parsing of the object from
     *  the byte stream. Concrete subclasses are expected to override this to provide
     *  class-specific parsing & initialization logic.
     */
    virtual void init(Client& client);

    /** Produce the Redis serialization protocol (RESP) representation. Must be overridden. */
    virtual String RESP() = 0;

    /** Produce a human-readable String representation. 
     *  Base implementation only returns the type character, so should be overriden. */
    virtual operator String() { return data; }

    Type type() const { return _type; } 

protected:
    String data;
    Type _type = Type::NoType;
};

/** A Simple String: https://redis.io/topics/protocol#resp-simple-strings */
class RedisSimpleString : public RedisObject {
public:
    RedisSimpleString(Client& c) : RedisObject(Type::SimpleString, c) {}
    ~RedisSimpleString() {}

    virtual String RESP() override;
};

/** A Bulk String: https://redis.io/topics/protocol#resp-bulk-strings */
class RedisBulkString : public RedisObject {
public:
    RedisBulkString(Client& c) : RedisObject(Type::BulkString) { init(c); }
    RedisBulkString(String& s) : RedisObject(Type::BulkString) { data = s; }
    ~RedisBulkString() {}

    virtual void init(Client& client) override;
   
    virtual String RESP() override;
};

/** An Array: https://redis.io/topics/protocol#resp-arrays */
class RedisArray : public RedisObject {
public:
    RedisArray() : RedisObject(Type::Array) {Serial.printf("RedisArray<%p> bCTOR %d\n", this, ESP.getFreeHeap());}
    RedisArray(Client& c) : RedisObject(Type::Array, c) { Serial.printf("RedisArray<%p> CTOR %d\n", this, ESP.getFreeHeap()); init(c); }
    ~RedisArray();

    void add(std::shared_ptr<RedisObject> param) { vec.push_back(param); }

    std::vector<String> strings() const;

    virtual void init(Client& client) override;

    virtual String RESP() override;

protected:
    std::vector<std::shared_ptr<RedisObject>> vec;
};

/** An Integer: https://redis.io/topics/protocol#resp-integers */
class RedisInteger : public RedisSimpleString {
public:
    RedisInteger(Client& c) : RedisSimpleString(c) { _type = Type::Integer; }
    ~RedisInteger() {}

    operator int() { return data.toInt(); }
    operator bool() { return (bool)operator int(); }
};

/** An Error: https://redis.io/topics/protocol#resp-errors */
class RedisError : public RedisSimpleString {
public:
    RedisError(Client& c) : RedisSimpleString(c) { _type = Type::Error; }
    ~RedisError() {}
};

/** An internal API error. Call RESP() to get the error string. */
class RedisInternalError : public RedisObject
{
public:
    RedisInternalError(String err) : RedisObject(Type::InternalError) { data = err; }
    RedisInternalError(const char* err) : RedisInternalError(String(err)) {}
    ~RedisInternalError() {}

    virtual String RESP() override { return "INTERNAL ERROR: " + data; }
};

/** A Command (a specialized Array subclass): https://redis.io/topics/protocol#sending-commands-to-a-redis-server */
class RedisCommand : public RedisArray {
public:
    RedisCommand(String command) : RedisArray() {Serial.printf("RedisCommand<%p> CTOR %d\n", this, ESP.getFreeHeap());
        add(std::shared_ptr<RedisObject>(new RedisBulkString(command)));
    }

    RedisCommand(String command, ArgList args)
        : RedisCommand(command)
    {Serial.printf("RedisCommand<%p> CTOR %d\n", this, ESP.getFreeHeap());
        for (auto arg : args) {
            add(std::shared_ptr<RedisObject>(new RedisBulkString(arg)));
        }
    }

    ~RedisCommand() {Serial.printf("RedisCommand<%p> DTOR %d\n", this, ESP.getFreeHeap());}

    /** Issue the command on the bytestream represented by `cmdClient`.
     *  @param cmdClient The client object representing the bytestream connection to a Redis server.
     *  @return A shared pointer of a "RedisObject" representing a concrete subclass instantiated as
     *  a result of parsing the Redis server return value. Check RedisObject::type() to determine concrete type.
     */
    std::shared_ptr<RedisObject> issue(Client& cmdClient);

    template <typename T>
    T issue_typed(Client& cmdClient);

private:
    String _err;
};
#endif // REDIS_INTERNAL_H
