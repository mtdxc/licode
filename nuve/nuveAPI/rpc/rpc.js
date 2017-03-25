/*global exports, require, setTimeout, clearTimeout*/
'use strict';
var amqp = require('amqp');
var rpcPublic = require('./rpcPublic');
var config = require('./../../../licode_config');
var logger = require('./../logger').logger;

// Logger
var log = logger.getLogger('RPC');

// Configuration default values
config.rabbit = config.rabbit || {};
config.rabbit.host = config.rabbit.host || 'localhost';
config.rabbit.port = config.rabbit.port || 5672;

var TIMEOUT = 3000;

var corrID = 0;
var rpc_map = {};   //{corrID: {fn: callback, to: timeout}}
var clientQueue;
var connection;
var exc;

// Create the amqp connection to rabbitMQ server
var addr = {};

if (config.rabbit.url !== undefined) {
    addr.url = config.rabbit.url;
} else {
    addr.host = config.rabbit.host;
    addr.port = config.rabbit.port;
}

if(config.rabbit.heartbeat !==undefined){
    addr.heartbeat = config.rabbit.heartbeat;
}

exports.connect = function (callback) {

    connection = amqp.createConnection(addr);

    connection.on('ready', function () {
        log.info('AMQP connected');

        //Create a direct exchange
        exc = connection.exchange('rpcExchange', {type: 'direct'}, function (exchange) {
            log.info('rpcExchange open, exchangeName: ' + exchange.name);

            var next = function() {
              //Create the queue for receive messages
              var q = connection.queue('nuveQueue', function (queue) {
                  log.info('queue open, queueName: ' + queue.name);

                  q.bind('rpcExchange', 'nuve');
                  q.subscribe(function (message) {
                      // map mqtt request message to rpcPublic
                      // {method: method, args: args, corrID: corrID, replyTo: clientQueue.name};
                      rpcPublic[message.method](message.args, function (type, result) {
                          exc.publish(message.replyTo, // send response
                                      {data: result, corrID: message.corrID, type: type});
                      });

                  });
                  if (callback) {
                      callback();
                  }
              });
            };

            //Create the queue for send messages(recv message?)
            clientQueue = connection.queue('', function (q) {
                log.info('clientQueue open, queueName: ' + q.name);
                clientQueue.bind('rpcExchange', clientQueue.name);
                clientQueue.subscribe(function (message) {
                    if (rpc_map[message.corrID] !== undefined) {
                        // call callback with response
                        rpc_map[message.corrID].fn[message.type](message.data);
                        clearTimeout(rpc_map[message.corrID].to);
                        delete rpc_map[message.corrID];
                    }
                });
                next();
            });
        });

    });

    connection.on('error', function(e) {
       log.error('AMQP connection error killing process, errorMsg: ' + logger.objectToLog(e));
       process.exit(1);
    });
};

var callbackError = function (corrID) {
    for (var i in rpc_map[corrID].fn) {
        rpc_map[corrID].fn[i]('timeout');
    }
    delete rpc_map[corrID];
};

/*
 * Calls remotely the 'method' function defined in rpcPublic of 'to'.
 */
exports.callRpc = function (to, method, args, callbacks) {
    corrID += 1;
    rpc_map[corrID] = {};
    rpc_map[corrID].fn = callbacks;
    rpc_map[corrID].to = setTimeout(callbackError, TIMEOUT, corrID);

    var send = {method: method, args: args, corrID: corrID, replyTo: clientQueue.name};

    exc.publish(to, send);

};
