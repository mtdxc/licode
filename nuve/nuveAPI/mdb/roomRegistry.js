/*global require, exports, ObjectId*/
'use strict';
var db = require('./dataBase').db;

var logger = require('./../logger').logger;

// Logger
var log = logger.getLogger('RoomRegistry');

exports.getRooms = function (callback) {
    db.rooms.find({}).toArray(function (err, rooms) {
        if (err || !rooms) {
            log.info('rooms list empty ' + err );
        } else {
            callback(rooms);
        }
    });
};

var getRoom = exports.getRoom = function (id, callback) {
    db.rooms.findOne({_id: db.ObjectId(id)}, function (err, room) {
        if (room === undefined) {
            log.warn('getRoom - Room not found, roomId: ' + id);
        }
        if (callback !== undefined) {
            callback(room);
        }
    });
};

var hasRoom = exports.hasRoom = function (id, callback) {
    getRoom(id, function (room) {
        if (room === undefined) {
            callback(false);
        } else {
            callback(true);
        }
    });
};

/*
 * Adds a new room to the data base.
 */
exports.addRoom = function (room, callback) {
    db.rooms.save(room, function (error, saved) {
        if (error) log.warn('addRoom error, ' + logger.objectToLog(error));
        callback(saved);
    });
};

exports.assignErizoControllerToRoom = function(room, erizoControllerId, callback) {
  return db.eval(function(id, erizoControllerId) {
    var erizoController;
    var room = db.rooms.findOne({_id: new ObjectId(id)});
    if (!room) {
      return erizoController;
    }
    // �ҵ�room������������erizoControllerId�򷵻أ�
    if (room.erizoControllerId) {
      erizoController = db.erizoControllers.findOne({_id: room.erizoControllerId});
      if (erizoController) {
        return erizoController;
      }
    }
    // ���� ����erizoControllerId������room��Ȼ�󷵻�
    erizoController = db.erizoControllers.findOne({_id: new ObjectId(erizoControllerId)});

    if (erizoController) {
      room.erizoControllerId = new ObjectId(erizoControllerId);

      db.rooms.save( room );
    }
    return erizoController;
  }, room._id + '', erizoControllerId + '', function(error, erizoController) {
    if (error) log.warn('assignErizoControllerToRoom error, ' + logger.objectToLog(error));
    if (callback) {
      callback(erizoController);
    }
  });
};

/*
 * Updates a determined room
 */
exports.updateRoom = function (id, room, callback) {
    db.rooms.update({_id: db.ObjectId(id)}, room, function (error) {
        if (error) log.warn('updateRoom error, ' + logger.objectToLog(error));
        if (callback) callback(error);
    });
};

/*
 * Removes a determined room from the data base.
 */
exports.removeRoom = function (id) {
    hasRoom(id, function (hasR) {
        if (hasR) {
            db.rooms.remove({_id: db.ObjectId(id)}, function (error) {
                if (error) log.warn('removeRoom error, ' + logger.objectToLog(error));
            });
        }
    });
};
