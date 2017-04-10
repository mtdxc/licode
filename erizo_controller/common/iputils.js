'use strict';

function getInterfaceAddrs(networkinterface, family){
    var addresses = [], address;
    var k, k2;
    try{
        var interfaces = require('os').networkInterfaces();
        for (k in interfaces) {
            if (interfaces.hasOwnProperty(k) && (!networkinterface || networkinterface === k)) {
                for (k2 in interfaces[k]) {
                    if (interfaces[k].hasOwnProperty(k2)) {
                        address = interfaces[k][k2];
                        if (!address.internal && (!family || address.family === family)) {
                           addresses.push(address.address);
                        }
                    }
                }
            }
        }
    } catch(err) {
        console.log('networkInterfaces error', err);
    }
    return addresses;
}

exports.v4Addrs = function(networkinterface){
   return getInterfaceAddrs(networkinterface, 'IPv4');
}

exports.v6Addrs = function(networkinterface){
   return getInterfaceAddrs(networkinterface, 'IPv6');
}

exports.getAwsAddr = function(callback){
    var AWS = require('aws-sdk');
    new AWS.MetadataService({
        httpOptions: {
            timeout: 5000
        }
    }).request('/latest/meta-data/public-ipv4', function(err, data) {
        if (err) {
            console.log('Error: ', err);
        } else {
            console.log('Got public ip: ', data);
            callback(data);
        }
    });
}