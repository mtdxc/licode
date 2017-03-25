'use strict';
var amqp = require('amqp');
var logger = require('./logger').logger;

// Logger
var log = logger.getLogger('AMQPER');

// Configuration default values
GLOBAL.config.rabbit = GLOBAL.config.rabbit || {};
GLOBAL.config.rabbit.host = GLOBAL.config.rabbit.host || 'localhost';
GLOBAL.config.rabbit.port = GLOBAL.config.rabbit.port || 5672;

var TIMEOUT = 5000;

// This timeout shouldn't be too low because it won't listen to onReady responses from ErizoJS
var REMOVAL_TIMEOUT = 300000;

var corrID = 0;
var rpc_map = {};   //{corrID: {fn: callback, to: timeout}}
var connection, clientQueue;
var rpcExc, broadcastExc;

var addr = {};
// callback function for public api
var rpcPublic = {};

if (GLOBAL.config.rabbit.url !== undefined) {
    addr.url = GLOBAL.config.rabbit.url;
} else {
    addr.host = GLOBAL.config.rabbit.host;
    addr.port = GLOBAL.config.rabbit.port;
}

if(GLOBAL.config.rabbit.heartbeat !==undefined){
    addr.heartbeat = GLOBAL.config.rabbit.heartbeat;
}

exports.setPublicRPC = function(methods) {
    rpcPublic = methods;
};

exports.connect = function(callback) {

    // Create the amqp connection to rabbitMQ server
    connection = amqp.createConnection(addr);
    connection.on('ready', function () {

        //Create a direct exchange
        rpcExc = connection.exchange('rpcExchange', {type: 'direct'}, function (exchange) {
            try {
                log.info('rpcExchange ' + exchange.name + ' open');

                //Create the queue for receiving messages
                clientQueue = connection.queue('', function (q) {
                    log.info('clientqueue ' + q.name +' open');

                    clientQueue.bind('rpcExchange', clientQueue.name, callback);

                    clientQueue.subscribe(function (message) {
                        try {
                            log.debug('queue ' + clientQueue.name  + ' message received: ' + logger.objectToLog(message));

                            if(rpc_map[message.corrID] !== undefined) {
                                log.debug('queue ' + clientQueue.name + ' callback ' +
                                          'type: ' + message.type + ',  ' + logger.objectToLog(message.data));
                                clearTimeout(rpc_map[message.corrID].to);
                                if (message.type === 'onReady') {
                                  rpc_map[message.corrID].fn[message.type].call({});
                                } else  {
                                  rpc_map[message.corrID].fn[message.type].call({}, message.data);
                                }
                                setTimeout(function() {
                                    if (rpc_map[message.corrID] !== undefined) {
                                      delete rpc_map[message.corrID];
                                    }
                                }, REMOVAL_TIMEOUT);
                            }
                        } catch(err) {
                            log.error('queue ' + clientQueue.name + ' error processing message ' +
                                      logger.objectToLog(err));
                        }
                    });

                });
            } catch (err) {
                log.error('message: exchange error, exchangeName: ' + exchange.name + ', ' + logger.objectToLog(err));
            }
        });

        //Create a fanout exchange
        broadcastExc = connection.exchange('broadcastExchange',
                                          {type: 'topic', autoDelete: false},
                                          function (exchange) {
            log.info('broadcastExchange open, exchangeName: ' + exchange.name);
        });
    });

    connection.on('error', function(e) {
       log.error('AMQP connection error killing process, ' + logger.objectToLog(e));
       process.exit(1);
    });
};

exports.bind = function(id, callback) {

    //Create the queue for receive messages
    var q = connection.queue(id, function () {
        try {
            log.info('queue ' + q.name +'/' + id + ' open');

            q.bind('rpcExchange', id, callback);
            q.subscribe(function (message) {
                try {
                    log.debug('queue '+ q.name +' message received: ' + logger.objectToLog(message));
                    message.args = message.args || [];
                    // 最后加一个回调函数参数,负责进行RPC Resopnse
                    message.args.push(function(type, result) {
                        rpcExc.publish(message.replyTo,
                                       {data: result, corrID: message.corrID, type: type});
                    });
                    // 调用Public函数
                    rpcPublic[message.method].apply(rpcPublic, message.args);
                } catch (error) {
                    log.error('queue '+ q.name + ' error processing call:' + logger.objectToLog(error));
                }

            });
        } catch (err) {
            log.error('queue '+ q.name + ' error: ' + logger.objectToLog(err));
        }

    });
};

//Subscribe to 'topic'
exports.bindBroadcast = function(id, callback) {

    //Create the queue for receive messages
    var q = connection.queue('', function () {
        try {
            log.info('broadcast queue '+ q.name + '/' + id + ' open');

            q.bind('broadcastExchange', id);
            q.subscribe(function (body){
                var answer;
                if (body.replyTo) {
                    answer = function (result) {
                        rpcExc.publish(body.replyTo, {data: result,
                                                      corrID: body.corrID,
                                                      type: 'callback'});
                    };
                }
                if (body.message.method && rpcPublic[body.message.method]) {
                    body.message.args.push(answer);
                    rpcPublic[body.message.method].apply(rpcPublic, body.message.args);
                } else {
                    callback(body.message, answer);
                }
            });

        } catch (err) {
            log.error('exchange error on queue ' + q.name + ': ' + logger.objectToLog(err));
        }

    });
};

var callbackError = function(corrID) {
    for (var i in rpc_map[corrID].fn) {
        rpc_map[corrID].fn[i]('timeout');
    }
    delete rpc_map[corrID];
};

/*
 * Publish broadcast messages to 'topic'
 * If message has the format {method: String, args: Array}. it will execute the RPC
 */
exports.broadcast = function(topic, message, callback) {
    var body = {message: message};

    if (callback) {
        corrID ++;
        rpc_map[corrID] = {};
        rpc_map[corrID].fn = {callback: callback};
        rpc_map[corrID].to = setTimeout(callbackError, TIMEOUT, corrID);

        body.corrID = corrID;
        body.replyTo = clientQueue.name;
    }
    broadcastExc.publish(topic, body);
};

/*
 * Calls remotely the 'method' function defined in rpcPublic of 'to'.
 */
exports.callRpc = function(to, method, args, callbacks) {
    corrID ++;
    rpc_map[corrID] = {};
    rpc_map[corrID].fn = callbacks;
    rpc_map[corrID].to = setTimeout(callbackError, TIMEOUT, corrID);
    rpcExc.publish(to, {method: method, args: args, corrID: corrID, replyTo: clientQueue.name});
};
