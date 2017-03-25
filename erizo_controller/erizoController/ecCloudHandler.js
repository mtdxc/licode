/*global require, exports, setInterval*/
'use strict';
var logger = require('./../common/logger').logger;

// Logger
var log = logger.getLogger('EcCloudHandler');

var EA_TIMEOUT = 30000;
var GET_EA_INTERVAL = 5000;
var AGENTS_ATTEMPTS = 5;
var WARN_UNAVAILABLE = 503, WARN_TIMEOUT = 504;
// 通过RPC与erizoAgent进行广播通讯,负责创建erizoJS的创建和释放，类似代理
exports.EcCloudHandler = function (spec) {
  var that = {},
  amqper = spec.amqper,
  agents = {};


  that.getErizoAgents = function () {
    amqper.broadcast('ErizoAgent', {method: 'getErizoAgents', args: []}, function (agent) {
      if (agent === 'timeout') {
        log.warn('no agents available, code: ' + WARN_UNAVAILABLE );
        return;
      }

      var newAgent = true;

      for (var a in agents) {
        if (a === agent.info.id) {
          // The agent is already registered, I update its stats and reset its
          agents[a].stats = agent.stats;
          agents[a].timeout = 0;
          newAgent = false;
        }
      }

      if (newAgent === true) {
        // New agent
        agents[agent.info.id] = agent;
        agents[agent.info.id].timeout = 0;
      }

    });

    // Check agents timeout
    for (var a in agents) {
      agents[a].timeout ++;
      if (agents[a].timeout > EA_TIMEOUT / GET_EA_INTERVAL) {
        log.warn('agent timed out is being removed, ' +
                 'code: ' + WARN_TIMEOUT + ', agentId: ' + agents[a].info.id);
        delete agents[a];
      }
    }
  };
  // agent的心跳检测
  setInterval(that.getErizoAgents, GET_EA_INTERVAL);

  var getErizoAgent;

  if (GLOBAL.config.erizoController.cloudHandlerPolicy) {
    getErizoAgent = require('./ch_policies/' +
                      GLOBAL.config.erizoController.cloudHandlerPolicy).getErizoAgent;
  }

  var createErizoJS = function (count, agentId, callback) {
    if (count >= AGENTS_ATTEMPTS) {
      callback('timeout');
      return;
    }

    if(count!=0)
      log.warn('agent selected timed out trying again, ' +
             'code: ' + WARN_TIMEOUT + ', agentId: ' + agentId);

    amqper.callRpc(agentId, 'createErizoJS', [], {callback: function(resp) {
      if (erizoId === 'timeout') {
        createErizoJS(++count, agentId ,callback);
      } else {
        var erizoId = resp.erizoId;
        var agentId = resp.agentId;
        log.info('createErizoJS success, erizoId: ' + erizoId + ', agentId: ' + agentId);
        callback(erizoId, agentId);
      }
    }});
  };

  that.getErizoJS = function(callback) {

    var agentQueue = 'ErizoAgent';
    if (getErizoAgent) {
      agentQueue = getErizoAgent(agents);
    }

    log.info('createErizoJS, agentId: ' + agentQueue);
    createErizoJS(0, agentQueue, callback);
  };

  that.deleteErizoJS = function(erizoId) {
    log.info ('deleting erizoJS, erizoId: ' + erizoId);
    amqper.broadcast('ErizoAgent', {method: 'deleteErizoJS', args: [erizoId]}, function(){});
  };

  return that;
};
