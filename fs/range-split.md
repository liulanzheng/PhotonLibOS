# range-split 说明

## 代码包含内容

此段代码主要提供给`XFile`及`AlignedFile`使用，用于对文件位置拆分分段的基本计算。

包含一个基础类`base_range_split`，定义了包括`aligned_parts_t`和`all_parts_t`两个遍历用迭代器，以及通用的计算。`base_range_split`不直接使用，需要写子类继承自`base_range_split`并实现构造函数、`divide(uint64_t, uint64_t&、uint64_t&、uint64_t&)`、`multiply(uint64_t, uint64_t)`及`get_length(uint64_T)`

## 基本概念

假设存在一个连续的一次线性空间，按照某种规则进行分段。range-split的作用为：给定空间上一条线段$[\mathrm{offset}, \mathrm{offset}+\mathrm{length})$，range-split能够提供对此线段所占用的所有分段的划分信息，包括分段编号、分段内起始位置及分段内长度。可以类比于硬盘DIO及内存对齐模型等。

特别的，对于一个小线段，如果它的两端都不与分界线对齐，则称之为`small_note`；当小线段只占用一个分段，且其末尾对齐而前部不对齐，则称其为`preface`；当只占用一个分段，且开头对齐而末尾不对齐，称其为`postface`；若占用满一个分段，两端均对齐，则称之为`aligned_part`。

对于占用超过一个分段的情况，则为：

```
<aligned_parts> ::= [<aligned_part>][<aligned_parts>]
<all_parts> ::= [<preface>][<aligned_parts>][<postface>]
```

一个文件可能全都是`aligned_part`，可能没有；可能在头部有一个`preface`，可能没有；可能在尾部有一个`postface`，也可能没有。

而`all_parts`方法将返回一个迭代器，依次遍历所有部分；`aligned_parts`方法返回迭代器，仅可遍历中段的所有已对齐部分。