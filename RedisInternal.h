#ifndef REDIS_INTERNAL_H
#define REDIS_INTERNAL_H

#include "Arduino.h"
#include "Client.h"
#include <vector>
#include <memory>
#include <functional>

#define CRLF F("\r\n")
#define ARDUINO_REDIS_SERIAL_TRACE  1

#define sprint(fmt, ...) do { if (ARDUINO_REDIS_SERIAL_TRACE) Serial.printf("[TRACE] " fmt, ##__VA_ARGS__); } while (0)

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

    RedisObject() {sprint("RedisObject CTOR1 %p\n", this);}
    RedisObject(Type tc) : _type(tc) {sprint("RedisObject CTOR2 %p\n", this);}
    RedisObject(Type tc, Client& c) : _type(tc) { sprint("RedisObject !!CTOR3!! %p\n", this);init(c); }
    
    virtual ~RedisObject() {sprint("RedisObject DTOR %p\n", this);}

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
    RedisSimpleString(Client& c) : RedisObject(Type::SimpleString, c) {sprint("RedisSimpleString CTOR1 %p\n", this);}
    ~RedisSimpleString() override {sprint("RedisSimpleString DTOR %p\n", this);}

    virtual String RESP() override;
};

/** A Bulk String: https://redis.io/topics/protocol#resp-bulk-strings */
class RedisBulkString : public RedisObject {
public:
    RedisBulkString(Client& c) : RedisObject(Type::BulkString, c) {sprint("RedisBulkString CTOR1 %p\n", this); init(c); }
    RedisBulkString(String& s) : RedisObject(Type::BulkString) { sprint("RedisBulkString CTOR2 %p\n", this);data = s; }
    ~RedisBulkString() override {sprint("RedisBulkString DTOR %p\n", this);}

    virtual void init(Client& client) override;
   
    virtual String RESP() override;
};

/** An Array: https://redis.io/topics/protocol#resp-arrays */
class RedisArray : public RedisObject {
public:
    RedisArray() : RedisObject(Type::Array) {sprint("RedisArray CTOR1 %p\n", this);}
    RedisArray(Client& c) : RedisObject(Type::Array, c) {sprint("RedisArray CTOR2 %p\n", this); init(c); }
    ~RedisArray() override;

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
    RedisInteger(Client& c) : RedisSimpleString(c) { sprint("RedisInteger CTOR1 %p\n", this);_type = Type::Integer; }
    ~RedisInteger() override {sprint("RedisInteger DTOR %p\n", this);}

    operator int() { return data.toInt(); }
    operator bool() { return (bool)operator int(); }
};

/** An Error: https://redis.io/topics/protocol#resp-errors */
class RedisError : public RedisSimpleString {
public:
    RedisError(Client& c) : RedisSimpleString(c) { _type = Type::Error; }
    ~RedisError() override {}
};

/** An internal API error. Call RESP() to get the error string. */
class RedisInternalError : public RedisObject
{
public:
    RedisInternalError(String err) : RedisObject(Type::InternalError) { data = err; }
    RedisInternalError(const char* err) : RedisInternalError(String(err)) {}
    ~RedisInternalError() override {}

    virtual String RESP() override { return "INTERNAL ERROR: " + data; }
};

/** A Command (a specialized Array subclass): https://redis.io/topics/protocol#sending-commands-to-a-redis-server */
class RedisCommand : public RedisArray {
public:
    RedisCommand(String command) : RedisArray() {sprint("RedisCommand<%p> CTOR %d\n", this, ESP.getFreeHeap());
        add(std::shared_ptr<RedisObject>(new RedisBulkString(command)));
    }

    RedisCommand(String command, ArgList args)
        : RedisCommand(command)
    {sprint("RedisCommand<%p> CTOR %d\n", this, ESP.getFreeHeap());
        for (auto arg : args) {
            add(std::shared_ptr<RedisObject>(new RedisBulkString(arg)));
        }
    }

    ~RedisCommand() override {sprint("RedisCommand<%p> DTOR %d\n", this, ESP.getFreeHeap());}

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
