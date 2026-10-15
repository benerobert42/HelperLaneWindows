const fs = require('fs');
const path = require('path');
const sharp = require('sharp'); // Install sharp or provide it through NODE_PATH.
(async () => {
  for (const name of ['screen_matrix_1x','screen_matrix_4x','core_comparison']) {
    await sharp(path.join(__dirname,name+'.svg'), {density:144}).png().toFile(path.join(__dirname,name+'.png'));
  }
})();
