const path = require('path');
const sharp = require('sharp'); // Install sharp or provide it through NODE_PATH.
(async () => {
  for (const name of ['performance','screen_matrix','geometry_gallery']) {
    await sharp(path.join(__dirname,name+'.svg'), {density:144}).png().toFile(path.join(__dirname,name+'.png'));
  }
})();
