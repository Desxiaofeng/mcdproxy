minecraft dynamic proxy gets first 64 bytes in tcp connection, then parses them to get hostname according to minecraft handshake protocol.

this work could be done by haproxy, but haproxy don't support dynamic backend server(you can workaround this by reloading config, but this means you have to write some scripts and deploy them...)

usage:
```
haproxy {}.test.com:25565 bc.{}.svc.local:25565
```
then a minecraft request with hostname ```xiaofeng.test.com:25565``` would be dynamicly proxied to ```bc.xiaofeng.svc.local:25565```

