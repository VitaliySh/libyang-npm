var yang = require("./build/Release/yang.node")

var ctx = yang.ly_ctx_new("./files");
var module = yang.lys_parse_path(ctx, "./files/hello.yin", yang.LYS_IN_YIN);
