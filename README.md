# Lumine2024 Shell

## 简介

一个简化版、类 PowerShell 风格的 Shell，使用 C++ 编写。

当前版本只实现并保证如下功能：
- 内置函数：`Set-Location`、`Write-Output`
- 执行外部程序
- 外部程序的 IO 重定向：`<`、`>`、`2>`、`&>`
- 管道：内置函数与外部程序可串接
- 强类型变量：`int`、`bool`、`string`
- 左结合表达式求值
- `if / elif / else`
- `while`

当前版本明确**不**实现如下功能：
- 自定义函数
- 格式化字符串
- 非 ASCII 输入

## 语法约束

这个项目优先保证执行功能，不追求复杂 parser，因此语法刻意收紧：

1. 除块语句外，一行只能有一条指令，不能把一条普通指令拆到多行。
2. 仅 `if`、`elif`、`else`、`while` 可以带代码块。
3. `if`、`elif`、`while` 的左大括号必须和关键字在同一行，且必须位于行尾。
4. `else` 只能写成 `else {`，不能带条件。
5. 右大括号必须独占一行，即整行只能是 `}`。
6. 变量首次赋值时必须声明类型，格式固定为 `$name[type] = expr`。
7. 变量再次赋值时格式固定为 `$name = expr`。
8. 表达式按**从左到右**求值，不支持优先级和括号。
9. 表达式中的 token 必须用空格分隔。
10. 字符串如果包含空格，必须写成双引号；双引号内部不支持转义。
11. 外部程序调用会先按当前 Shell 的分词规则拆分参数，再直接创建进程；如果路径或参数包含空格，请自行加双引号。
12. 当前只有外部程序支持文件重定向；内置函数暂不支持。
13. 管道中只允许出现内置函数和外部程序；块语句和赋值语句不能进入管道。
14. 当前管道不支持 stage 自带的显式 stdin/stdout 文件重定向。
15. 关键字和内置函数区分大小写，推荐严格按照本文示例书写。

## 支持的值

- 整数字面量：`1`、`-7`
- 布尔字面量：`true`、`false`
- 字符串字面量：`"hello world"`、`abc`
- 变量引用：`$i`

说明：
- 不带引号的普通单词在表达式里会按 `string` 处理。
- 因此 `Write-Output Hello-world` 是合法的。

## 支持的表达式操作

### `int`

- `-Add`
- `-Sub`
- `-Mul`
- `-Div`
- `-Rem`
- `-Eq`
- `-Ne`
- `-Gt`
- `-Ge`
- `-Lt`
- `-Le`
- 一元 `-Neg`

### `bool`

- `-And`
- `-Or`
- `-Eq`
- `-Ne`
- 一元 `-Not`

### `string`

- `-Add`
- `-Eq`
- `-Ne`
- `-Gt`
- `-Ge`
- `-Lt`
- `-Le`

## 内置函数

### `Set-Location`

切换当前工作目录：

```powershell
Set-Location .
Set-Location ".."
Set-Location "C:\Windows"
```

### `Write-Output`

输出一个表达式的结果：

```powershell
Write-Output 123
Write-Output $name
Write-Output "hello" -Add " world"
```

## 分支和循环

### `if / elif / else`

```powershell
if $a -Gt 0 {
    Write-Output positive
}
elif $a -Eq 0 {
    Write-Output zero
}
else {
    Write-Output negative
}
```

### `while`

```powershell
$i[int] = 0
while $i -Lt 3 {
    Write-Output $i
    $i = $i -Add 1
}
```

## 完整示例

```powershell
$msg[string] = "hello"
$i[int] = 1
$ok[bool] = true

Write-Output $msg -Add " shell"

if $ok -And true {
    Write-Output yes
}
else {
    Write-Output no
}

while $i -Le 3 {
    Write-Output $i
    $i = $i -Add 1
}

python -c "print('external-ok')"
```

## 管道示例

```powershell
"hello_and_again_and_again" | python tests/helpers/echo_stdin.py | python tests/helpers/echo_stdin.py
```

说明：
- 管道中的单独字符串字面量会按 `Write-Output "..."` 处理，便于快速构造输入。
