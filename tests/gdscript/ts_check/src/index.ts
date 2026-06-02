import path from "path";
import { promises as fs } from "fs";
import { program } from "commander";
import { Builder, ByteBuffer } from "flatbuffers";
import { MyFirstEnum, MyRoot, SubTable, Vec } from "./generated/simple";

async function writeFileBinary(
  filepath: string,
  data: Uint8Array
): Promise<boolean> {
  try {
    const fullPath = `./out/${filepath}`;
    const dirName = path.dirname(fullPath);

    await fs.mkdir(dirName, { recursive: true });
    await fs.writeFile(fullPath, data, "binary");
    return true;
  } catch (e) {
    console.error(e);
    return false;
  }
}


async function main() {
  program.name("gen binary")
    // .option(
    //   "-s, --source-dir <string>",
    //   "Name of local source dir with GLB files",
    //   "../../source-data/ballpark-videos",
    // )

  program.parse(process.argv);
  // const options = program.opts();
  // console.log(options);

  // await fs.mkdir(options.destDir, { recursive: true });

  // const transformer = new Copier(
  //   options.sourceDir, options.destDir, options.extension
  // );

  const builder = new Builder(1024)

  const num = 100
  const arrOffset = SubTable.createArrVector(builder, [10, 9, 8, 7, 6])
  // console.log("create str")
  const strOffset = builder.createString("aiden")

  SubTable.startSubTable(builder)
  SubTable.addNum(builder, num)
  const vecOffset = Vec.createVec(builder, 1, 2, 3)
  SubTable.addVec(builder, vecOffset)
  SubTable.addArr(builder, arrOffset)
  SubTable.addNint(builder, -90)
  SubTable.addBint(builder, 12345)
  SubTable.addStr_(builder, strOffset)
  SubTable.addE(builder, MyFirstEnum.One)
  const dataOffset = SubTable.endSubTable(builder)

  const uidOffset = builder.createString("1234abcd")
  MyRoot.startMyRoot(builder)
  MyRoot.addUid(builder, uidOffset)
  MyRoot.addData(builder, dataOffset)
  const myRoot = MyRoot.endMyRoot(builder)

  MyRoot.finishMyRootBuffer(builder, myRoot)
  const buf = builder.asUint8Array()
  writeFileBinary("ts-out.bin", buf)


  // double check...
  const fb = new ByteBuffer(buf)
  const root = MyRoot.getRootAsMyRoot(fb)
  console.log("TS CHECK:")
  const uid = root.uid()
  console.log("uid:", uid)

  const data = root.data()
  console.log("num:", data.num())
  console.log("arr len:", data.arrLength())
  console.log("arr:", data.arrArray())
  let arrStr = "["
  for (let i = 0; i < data.arrLength(); i++) {
    arrStr += data.arr(i) + ", "
  }
  arrStr += "]"
  console.log("arr:", arrStr)

  const vec = data.vec()
  console.log("vec:", vec.x(), vec.y(), vec.z())
  console.log("str:", data.str_())

  process.exitCode = 0;
}

main();
