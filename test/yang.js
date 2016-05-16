var yang = require("../build/Release/yang.node")

var ctx = yang.ly_ctx_new("./files");
var module = yang.lys_parse_path(ctx, "./files/hello@2015-06-08.yin", yang.LYS_IN_YIN);
var node = yang.lyd_parse_path(ctx, "./files/hello.xml", yang.LYD_XML, 0);
